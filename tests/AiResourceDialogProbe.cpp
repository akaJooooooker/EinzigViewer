#include "AiResourceDialog.h"
#include "../resources/AiResourceDialogIds.h"
#include <commctrl.h>
#include <atomic>
#include <chrono>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
constexpr std::uint64_t MiB = 1024ULL * 1024, GiB = 1024ULL * MiB;
void Require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
std::wstring Text(HWND window) {
    std::wstring text(static_cast<std::size_t>(GetWindowTextLengthW(window)) + 1, L'\0');
    text.resize(GetWindowTextW(window, text.data(), static_cast<int>(text.size())));
    return text;
}
void Click(HWND dialog, int id) { SendMessageW(GetDlgItem(dialog, id), BM_CLICK, 0, 0); }
ai::ResourceSnapshot Healthy() {
    ai::ResourceSnapshot s;
    s.memoryKnown = s.diskKnown = s.gpuKnown = true;
    s.totalPhysical = 64 * GiB; s.availablePhysical = s.availableCommit = 40 * GiB;
    s.diskTotal = 1000 * GiB; s.diskAvailable = 250 * GiB;
    s.gpuTotal = 16 * GiB; s.gpuAvailable = 8 * GiB;
    return s;
}
ai::ResourceProbe Probe() { return [](const std::filesystem::path&, int) { return Healthy(); }; }
bool Ready(HWND dialog) { return IsWindowEnabled(GetDlgItem(dialog, IDC_AI_REFRESH)) != FALSE; }

// Hooks only dialogs created by this test's own UI thread; no global input.
class DialogScript {
public:
    using Step = std::function<bool(HWND)>;
    explicit DialogScript(std::vector<Step> steps) : steps_(std::move(steps)) {
        Require(!current_, "only one dialog script on this thread"); current_ = this;
        hook_ = SetWindowsHookExW(WH_CALLWNDPROCRET, Hook, nullptr, GetCurrentThreadId());
        Require(hook_ != nullptr, "install own-thread dialog hook");
    }
    ~DialogScript() { UnhookWindowsHookEx(hook_); current_ = nullptr; }
    void Verify() const {
        if (!failure_.empty()) throw std::runtime_error(failure_);
        Require(found_, "resource settings created a native dialog");
        Require(next_ == steps_.size(), "all resource dialog steps ran");
    }
private:
    static constexpr UINT_PTR timerId = 5910;
    static inline thread_local DialogScript* current_ = nullptr;
    static LRESULT CALLBACK Hook(int code, WPARAM wParam, LPARAM lParam) {
        if (code >= 0 && current_) {
            const auto message = reinterpret_cast<CWPRETSTRUCT*>(lParam);
            if (message->message == WM_INITDIALOG && GetDlgItem(message->hwnd, IDC_AI_RAM)) {
                auto& self = *current_; self.found_ = true;
                self.deadline_ = GetTickCount64() + 6000;
                SetWindowSubclass(message->hwnd, Subclass, 1, reinterpret_cast<DWORD_PTR>(&self));
                SetTimer(message->hwnd, timerId, 15, nullptr);
            }
        }
        return CallNextHookEx(nullptr, code, wParam, lParam);
    }
    static LRESULT CALLBACK Subclass(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam,
        UINT_PTR id, DWORD_PTR reference) {
        auto& self = *reinterpret_cast<DialogScript*>(reference);
        if (message == WM_TIMER && wParam == timerId) {
            try {
                Require(GetTickCount64() < self.deadline_, "resource dialog script timed out");
                if (self.next_ < self.steps_.size() && self.steps_[self.next_](dialog)) ++self.next_;
            } catch (const std::exception& error) {
                self.failure_ = error.what(); SendMessageW(dialog, WM_COMMAND, IDCANCEL, 0);
            }
            return 0;
        }
        if (message == WM_NCDESTROY) { KillTimer(dialog, timerId); RemoveWindowSubclass(dialog, Subclass, id); }
        return DefSubclassProc(dialog, message, wParam, lParam);
    }
    std::vector<Step> steps_;
    std::size_t next_ = 0;
    ULONGLONG deadline_ = 0;
    HHOOK hook_ = nullptr;
    bool found_ = false;
    std::string failure_;
};

void CheckLayout(HWND dialog) {
    RECT window{}; GetWindowRect(dialog, &window);
    const auto dpi = GetDpiForWindow(dialog);
    Require(dpi != 0, "dialog has a measurable DPI");
    const auto heightAt144Dpi = MulDiv(window.bottom - window.top, 144, static_cast<int>(dpi));
    if (heightAt144Dpi > 1000)
        throw std::runtime_error("resource dialog is too tall at 150% DPI: " + std::to_string(heightAt144Dpi) + "px (maximum 1000px)");
    RECT client{}; GetClientRect(dialog, &client);
    for (auto child = GetWindow(dialog, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
        RECT rectangle{}; GetWindowRect(child, &rectangle); MapWindowPoints(nullptr, dialog, reinterpret_cast<POINT*>(&rectangle), 2);
        Require(rectangle.left >= 0 && rectangle.top >= 0 && rectangle.right <= client.right && rectangle.bottom <= client.bottom,
            "localized controls fit dialog at current DPI");
        const auto id = GetDlgCtrlID(child);
        if (id == IDC_AI_RAM || id == IDC_AI_DISK || id == IDC_AI_CPU || id == IDC_AI_BATCH || id == IDC_AI_GPU) continue;
        auto text = Text(child); if (text.empty()) continue;
        auto dc = GetDC(child); auto old = SelectObject(dc, reinterpret_cast<HFONT>(SendMessageW(child, WM_GETFONT, 0, 0)));
        RECT bounds{}; GetClientRect(child, &bounds);
        const int height = bounds.bottom;
        DrawTextW(dc, text.c_str(), -1, &bounds, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
        SelectObject(dc, old); ReleaseDC(child, dc);
        Require(bounds.bottom <= height, "Chinese and English text fits allocated control height");
    }
}

void ModesAndValidation(const std::filesystem::path& root) {
    for (const bool english : {false, true}) {
        DialogScript script({[=](HWND dialog) {
            if (!Ready(dialog)) return false;
            Require(Text(dialog) == (english ? L"AI resource settings" : L"AI 資源設定"), "localized resource dialog title");
            Require(IsDlgButtonChecked(dialog, IDC_AI_AUTOMATIC) == BST_CHECKED && !IsWindowEnabled(GetDlgItem(dialog, IDC_AI_CPU)),
                "automatic is default and manual edits disabled");
            const auto resources = Text(GetDlgItem(dialog, IDC_AI_RESOURCES));
            Require(resources.find(L"64") != std::wstring::npos && resources.find(L"250") != std::wstring::npos && resources.find(L"8") != std::wstring::npos,
                "one-shot snapshot displays actual RAM, disk and GPU capacity");
            Require(!Text(GetDlgItem(dialog, IDC_AI_EFFECTIVE)).empty(), "effective budgets shown");
            RECT bounds{}; GetWindowRect(dialog, &bounds);
            const auto dpi = GetDpiForWindow(dialog);
            std::cout << "LAYOUT " << (english ? "en" : "zh") << " dpi=" << dpi << " window=" << bounds.right - bounds.left << 'x' << bounds.bottom - bounds.top
                << " heightAt144Dpi=" << MulDiv(bounds.bottom - bounds.top, 144, static_cast<int>(dpi)) << "px\n";
            CheckLayout(dialog); Click(dialog, IDC_AI_MANUAL);
            Require(IsWindowEnabled(GetDlgItem(dialog, IDC_AI_CPU)), "manual mode enables edits");
            SetDlgItemTextW(dialog, IDC_AI_CPU, L"0"); Click(dialog, IDOK); return true;
        }, [](HWND dialog) {
            Require(!Text(GetDlgItem(dialog, IDC_AI_ERROR)).empty() && Text(GetDlgItem(dialog, IDC_AI_CPU)) == L"0", "invalid CPU stays editable with inline error");
            CheckLayout(dialog);
            SetDlgItemTextW(dialog, IDC_AI_CPU, L"100"); SetDlgItemTextW(dialog, IDC_AI_RAM, L"16");
            SetDlgItemTextW(dialog, IDC_AI_DISK, L"24"); SetDlgItemTextW(dialog, IDC_AI_BATCH, L"8");
            SetDlgItemTextW(dialog, IDC_AI_GPU, L"2048"); Click(dialog, IDOK); return true;
        }});
        const auto result = ai::ShowResourceDialog(nullptr, english, {}, root, Probe()); script.Verify();
        Require(result && result->manual && result->ramBytes == 16 * GiB && result->diskBytes == 24 * GiB &&
            result->cpuPercent == 100 && result->batchPages == 8 && result->gpuReserveBytes == 2048 * MiB, "valid manual fields returned without fixed RAM/cache cap");
        const auto path = root / (english ? L"english.ini" : L"chinese.ini"); std::wstring error;
        Require(ai::WriteUserLimits(path, *result, error), "persist explicitly applied result");
        const auto reload = ai::ReadUserLimits(path);
        Require(reload.manual && reload.ramBytes == 16 * GiB && reload.cpuPercent == 100 && reload.error.empty(), "applied values survive reload");
    }
    for (const auto& [id, value] : std::vector<std::pair<int, std::wstring>>{
        {IDC_AI_CPU,L"101"},{IDC_AI_CPU,L"-1"},{IDC_AI_CPU,L"25x"},{IDC_AI_CPU,L"1.5"},{IDC_AI_CPU,L""},
        {IDC_AI_RAM,L"0"},{IDC_AI_RAM,L"65"},{IDC_AI_RAM,L"18446744073709551616"},
        {IDC_AI_DISK,L"1001"},{IDC_AI_DISK,L"-1"},{IDC_AI_BATCH,L"0"},{IDC_AI_BATCH,L"9"},{IDC_AI_GPU,L"511"}}) {
        DialogScript script({[=](HWND dialog) {
            if (!Ready(dialog)) return false;
            Click(dialog, IDC_AI_MANUAL); SetDlgItemTextW(dialog, id, value.c_str()); Click(dialog, IDOK); return true;
        }, [=](HWND dialog) {
            Require(!Text(GetDlgItem(dialog, IDC_AI_ERROR)).empty() && Text(GetDlgItem(dialog, id)) == value,
                "invalid number/range/capacity cannot close dialog or discard input"); CheckLayout(dialog); Click(dialog, IDCANCEL); return true;
        }});
        Require(!ai::ShowResourceDialog(nullptr, true, {}, root, Probe()), "cancel invalid input returns no settings"); script.Verify();
    }
}

void CancelResetAndProbeLifetime(const std::filesystem::path& root) {
    ai::UserLimits initial; initial.manual = true; initial.cpuPercent = 37;
    {
        DialogScript script({[](HWND dialog) { SetDlgItemTextW(dialog, IDC_AI_CPU, L"80"); SendMessageW(dialog, WM_CLOSE, 0, 0); return true; }});
        Require(!ai::ShowResourceDialog(nullptr, true, initial, root, Probe()) && initial.cpuPercent == 37, "close cancels edits without mutating caller"); script.Verify();
    }
    {
        DialogScript script({[](HWND dialog) { PostMessageW(dialog, WM_KEYDOWN, VK_ESCAPE, 0); return true; }});
        Require(!ai::ShowResourceDialog(nullptr, true, initial, root, Probe()), "Escape cancels native settings dialog"); script.Verify();
    }
    {
        initial.error = L"Invalid saved settings";
        DialogScript script({[](HWND dialog) {
            Require(Text(GetDlgItem(dialog, IDC_AI_ERROR)).find(L"Invalid saved settings") != std::wstring::npos, "invalid saved settings visible");
            Click(dialog, IDC_AI_RESET); Click(dialog, IDOK); return true;
        }});
        const auto reset = ai::ShowResourceDialog(nullptr, false, initial, root, Probe()); script.Verify();
        Require(reset && !reset->manual && reset->error.empty(), "explicit reset clears invalid persisted settings to automatic");
    }
    {
        DialogScript script({[](HWND dialog) {
            if (!Ready(dialog)) return false;
            Click(dialog, IDC_AI_MANUAL); SetDlgItemTextW(dialog, IDC_AI_GPU, L"512");
            Require(Text(GetDlgItem(dialog, IDC_AI_GPU_NOTE)).find(L"paus") != std::wstring::npos, "unknown GPU clearly explains explicit threshold pauses AI");
            Click(dialog, IDOK); return true;
        }});
        const auto result = ai::ShowResourceDialog(nullptr, true, {}, root, [](const auto&, int) { auto s = Healthy(); s.gpuKnown = false; return s; });
        script.Verify(); Require(result && result->gpuReserveBytes == 512 * MiB, "unknown GPU threshold remains saveable");
    }
    {
        DialogScript script({[](HWND dialog) {
            if (!Ready(dialog)) return false;
            Click(dialog, IDC_AI_MANUAL); Click(dialog, IDOK); return true;
        }, [](HWND dialog) {
            Require(!Text(GetDlgItem(dialog, IDC_AI_ERROR)).empty(), "missing capacity probe blocks manual apply");
            Click(dialog, IDC_AI_AUTOMATIC); Click(dialog, IDOK); return true;
        }});
        const auto result = ai::ShowResourceDialog(nullptr, true, {}, root, [](const auto&, int) { return ai::ResourceSnapshot{}; });
        script.Verify(); Require(result && !result->manual, "automatic still usable when capacity is unknown");
    }
    {
        auto entered = std::make_shared<std::atomic_bool>(false), release = std::make_shared<std::atomic_bool>(false), finished = std::make_shared<std::atomic_bool>(false);
        DialogScript script({[=](HWND dialog) {
            if (!entered->load()) return false;
            Click(dialog, IDCANCEL); return true;
        }});
        const auto start = GetTickCount64();
        const auto result = ai::ShowResourceDialog(nullptr, true, {}, root, [=](const auto&, int) {
            entered->store(true); while (!release->load()) std::this_thread::sleep_for(std::chrono::milliseconds(2));
            finished->store(true); return Healthy();
        });
        script.Verify(); const auto elapsed = GetTickCount64() - start;
        release->store(true);
        while (!finished->load()) std::this_thread::sleep_for(std::chrono::milliseconds(2));
        Require(!result && elapsed < 500, "closing while resource probe is blocked returns promptly");
        // Pump after completion to expose accidental posts to the destroyed dialog.
        MSG message{}; while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&message);
    }
    {
        auto calls = std::make_shared<std::atomic_int>(0);
        DialogScript script({[=](HWND dialog) {
            if (!Ready(dialog)) return false;
            Require(calls->load() == 1, "open probes only once"); Click(dialog, IDC_AI_REFRESH); return true;
        }, [=](HWND dialog) {
            if (!Ready(dialog)) return false;
            Require(calls->load() == 2, "refresh performs one additional probe"); Click(dialog, IDCANCEL); return true;
        }});
        ai::ShowResourceDialog(nullptr, true, {}, root, [=](const auto&, int) { ++*calls; return Healthy(); }); script.Verify();
        const auto after = calls->load(); std::this_thread::sleep_for(std::chrono::milliseconds(150));
        Require(calls->load() == after, "closed resource UI performs no periodic probing");
    }
}
}
int wmain(int argc, wchar_t** argv) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES}; InitCommonControlsEx(&controls);
    try {
        if (argc == 2 && (std::wstring(argv[1]) == L"--preview-zh" || std::wstring(argv[1]) == L"--preview-en")) {
            ai::UserLimits manual; manual.manual = true;
            ai::ShowResourceDialog(nullptr, std::wstring(argv[1]) == L"--preview-en", manual, std::filesystem::temp_directory_path());
            return 0;
        }
        const auto root = std::filesystem::absolute(argc > 1 ? argv[1] : L"tmp/ai-resource-dialog-probe"); std::filesystem::create_directories(root);
        ModesAndValidation(root); CancelResetAndProbeLifetime(root);
        std::cout << "PASS native AI resource dialog: bilingual layout, modes, strict input/capacity, persist/reload, cancel/reset, unknown GPU, refresh, detached probe lifetime.\n"; return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
