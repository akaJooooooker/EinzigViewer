#include "AiResourceDialog.h"
#include "../resources/AiResourceDialogIds.h"

#include <algorithm>
#include <atomic>
#include <iomanip>
#include <limits>
#include <memory>
#include <sstream>
#include <thread>

namespace ai {
namespace {
constexpr std::uint64_t MiB = 1024ULL * 1024, GiB = 1024ULL * MiB;
constexpr UINT_PTR ProbeTimer = 77;
constexpr int Fields[] = {IDC_AI_RAM, IDC_AI_DISK, IDC_AI_CPU, IDC_AI_BATCH, IDC_AI_GPU};

struct ProbeResult {
    ResourceSnapshot snapshot;
    std::wstring error;
    std::atomic_bool done{false};
};
struct State {
    bool english;
    UserLimits initial;
    std::filesystem::path root;
    ResourceProbe probe;
    std::shared_ptr<ProbeResult> pending;
    ResourceSnapshot snapshot;
    bool sampled = false, filling = false;
    std::optional<UserLimits> result;
};
const wchar_t* Local(const State& state, const wchar_t* chinese, const wchar_t* english) {
    return state.english ? english : chinese;
}
void Error(HWND dialog, const std::wstring& text) { SetDlgItemTextW(dialog, IDC_AI_ERROR, text.c_str()); }
std::wstring Text(HWND dialog, int id) {
    const auto control = GetDlgItem(dialog, id);
    std::wstring value(static_cast<std::size_t>(GetWindowTextLengthW(control)) + 1, L'\0');
    value.resize(GetWindowTextW(control, value.data(), static_cast<int>(value.size())));
    return value;
}
bool Number(const std::wstring& text, std::uint64_t& result) {
    if (text.empty()) return false;
    result = 0;
    for (const auto digit : text) {
        if (digit < L'0' || digit > L'9') return false;
        const auto number = static_cast<unsigned>(digit - L'0');
        if (result > (std::numeric_limits<std::uint64_t>::max() - number) / 10) return false;
        result = result * 10 + number;
    }
    return true;
}
bool ReadInput(HWND dialog, const State& state, UserLimits& value, std::wstring& error, bool capacity) {
    value = {};
    value.manual = IsDlgButtonChecked(dialog, IDC_AI_MANUAL) == BST_CHECKED;
    if (!value.manual) return true;
    std::uint64_t values[5]{};
    for (std::size_t i = 0; i < std::size(Fields); ++i) {
        if (!Number(Text(dialog, Fields[i]), values[i])) {
            error = Local(state, L"請使用完整的非負整數，不可留白、小數、負號或其他字元。", L"Enter whole nonnegative integers; blanks, decimals, signs and extra characters are invalid.");
            return false;
        }
    }
    if (values[0] > std::numeric_limits<std::uint64_t>::max() / GiB ||
        values[1] > std::numeric_limits<std::uint64_t>::max() / GiB ||
        values[4] > std::numeric_limits<std::uint64_t>::max() / MiB || values[2] > 100 || values[3] > 8) {
        error = Local(state, L"數值超出範圍：CPU 1–100%、批次 1–8；記憶體與快取請填本機容量內的整數。", L"Value out of range: CPU 1–100%, batch 1–8; RAM and cache must fit local capacity.");
        return false;
    }
    value.ramBytes = values[0] * GiB; value.diskBytes = values[1] * GiB;
    value.cpuPercent = static_cast<unsigned>(values[2]); value.batchPages = static_cast<unsigned>(values[3]);
    value.gpuReserveBytes = values[4] * MiB;
    if (!ValidateUserLimits(value, error)) return false;
    if (capacity) {
        if (!state.sampled || !state.snapshot.memoryKnown || !state.snapshot.diskKnown ||
            !state.snapshot.totalPhysical || !state.snapshot.diskTotal) {
            error = Local(state, L"尚未確認 RAM 與暫存磁碟總容量；請先讀取目前資源，或選擇自動。", L"RAM and temporary disk capacity are unconfirmed. Refresh resources first, or select Automatic.");
            return false;
        }
        if (value.ramBytes > state.snapshot.totalPhysical || value.diskBytes > state.snapshot.diskTotal) {
            error = Local(state, L"RAM／快取上限不可超過已確認的本機 RAM／暫存磁碟總容量。", L"RAM/cache limits cannot exceed confirmed local RAM/temporary disk total capacity.");
            return false;
        }
    }
    return true;
}
std::wstring Amount(std::uint64_t bytes) {
    std::wostringstream text;
    text << std::fixed << std::setprecision(1) << static_cast<double>(bytes) / GiB << L" GiB";
    return text.str();
}
void DisplaySnapshot(HWND dialog, const State& state) {
    const auto& s = state.snapshot;
    const std::wstring unknown = Local(state, L"未知", L"unknown");
    const auto pair = [&](bool known, std::uint64_t available, std::uint64_t total) {
        return known ? Amount(available) + L" / " + Amount(total) : unknown;
    };
    const auto text = std::wstring(Local(state, L"可用／總量：RAM ", L"Available / total: RAM ")) +
        pair(s.memoryKnown, s.availablePhysical, s.totalPhysical) + Local(state, L"；可用 commit ", L"; commit available ") +
        (s.memoryKnown ? Amount(s.availableCommit) : unknown) + L"\r\n" +
        Local(state, L"暫存磁碟 ", L"Temporary disk ") + pair(s.diskKnown, s.diskAvailable, s.diskTotal) +
        L"; GPU " + pair(s.gpuKnown, s.gpuAvailable, s.gpuTotal);
    SetDlgItemTextW(dialog, IDC_AI_RESOURCES, text.c_str());
}
void Preview(HWND dialog, const State& state) {
    UserLimits value; std::wstring error;
    if (!ReadInput(dialog, state, value, error, false)) {
        SetDlgItemTextW(dialog, IDC_AI_EFFECTIVE, Local(state, L"請輸入有效上限以預覽實際配額。", L"Enter valid limits to preview effective budgets."));
        return;
    }
    const auto automatic = ComputeLimits(state.snapshot);
    const auto effective = ComputeLimits(state.snapshot, 0, value);
    auto text = std::wstring(Local(state, L"自動：RAM 池 ", L"Automatic: RAM pool ")) + Amount(automatic.totalBytes) +
        Local(state, L"；快取 ", L"; cache ") + Amount(automatic.diskBytes) + L"\r\n" +
        Local(state, L"目前實際：RAM 池 ", L"Effective now: RAM pool ") + Amount(effective.totalBytes) +
        Local(state, L"；快取 ", L"; cache ") + Amount(effective.diskBytes) + L"; CPU " + std::to_wstring(effective.cpuPercent) +
        L"%; " + std::to_wstring(effective.batchPages) + Local(state, L" 頁／批", L" pages/batch");
    SetDlgItemTextW(dialog, IDC_AI_EFFECTIVE, text.c_str());
}
void EnableFields(HWND dialog) {
    const bool manual = IsDlgButtonChecked(dialog, IDC_AI_MANUAL) == BST_CHECKED;
    for (const auto id : Fields) EnableWindow(GetDlgItem(dialog, id), manual);
}
void Fill(HWND dialog, State& state, const UserLimits& value) {
    state.filling = true;
    CheckRadioButton(dialog, IDC_AI_AUTOMATIC, IDC_AI_MANUAL, value.manual ? IDC_AI_MANUAL : IDC_AI_AUTOMATIC);
    SetDlgItemTextW(dialog, IDC_AI_RAM, std::to_wstring(value.ramBytes / GiB).c_str());
    SetDlgItemTextW(dialog, IDC_AI_DISK, std::to_wstring(value.diskBytes / GiB).c_str());
    SetDlgItemTextW(dialog, IDC_AI_CPU, std::to_wstring(value.cpuPercent).c_str());
    SetDlgItemTextW(dialog, IDC_AI_BATCH, std::to_wstring(value.batchPages).c_str());
    SetDlgItemTextW(dialog, IDC_AI_GPU, std::to_wstring(value.gpuReserveBytes / MiB).c_str());
    EnableFields(dialog); state.filling = false;
}
void BeginProbe(HWND dialog, State& state) {
    if (state.pending) return;
    auto result = std::make_shared<ProbeResult>();
    state.pending = result;
    EnableWindow(GetDlgItem(dialog, IDC_AI_REFRESH), FALSE);
    SetDlgItemTextW(dialog, IDC_AI_RESOURCES, Local(state, L"正在讀取目前資源…（不啟動模型）", L"Reading current resources… (no model is started)"));
    try {
        // A detached probe owns only values and this shared result. It never
        // captures the dialog, State, App, or any HWND and never posts messages.
        std::thread([result, root = state.root, probe = state.probe] {
            try { result->snapshot = probe(root, -1); }
            catch (...) { result->error = L"資源讀取失敗；可重試。 / Resource probe failed; try Refresh."; }
            result->done.store(true, std::memory_order_release);
        }).detach();
        SetTimer(dialog, ProbeTimer, 40, nullptr);
    } catch (...) {
        state.pending.reset(); EnableWindow(GetDlgItem(dialog, IDC_AI_REFRESH), TRUE);
        Error(dialog, Local(state, L"無法啟動資源讀取。", L"Could not start the resource probe."));
    }
}
void Center(HWND dialog, HWND owner) {
    MONITORINFO monitor{sizeof(monitor)};
    if (!GetMonitorInfoW(MonitorFromWindow(owner ? owner : dialog, MONITOR_DEFAULTTONEAREST), &monitor)) return;
    RECT anchor = monitor.rcWork, bounds{};
    if (owner && IsWindowVisible(owner)) GetWindowRect(owner, &anchor);
    GetWindowRect(dialog, &bounds);
    const auto width = bounds.right - bounds.left, height = bounds.bottom - bounds.top;
    const auto x = std::clamp(anchor.left + (anchor.right - anchor.left - width) / 2,
        monitor.rcWork.left, std::max(monitor.rcWork.left, monitor.rcWork.right - width));
    const auto y = std::clamp(anchor.top + (anchor.bottom - anchor.top - height) / 2,
        monitor.rcWork.top, std::max(monitor.rcWork.top, monitor.rcWork.bottom - height));
    SetWindowPos(dialog, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}
INT_PTR CALLBACK Procedure(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam) {
    auto state = reinterpret_cast<State*>(GetWindowLongPtrW(dialog, DWLP_USER));
    if (message == WM_INITDIALOG) {
        state = reinterpret_cast<State*>(lParam);
        SetWindowLongPtrW(dialog, DWLP_USER, reinterpret_cast<LONG_PTR>(state));
        SetWindowTextW(dialog, Local(*state, L"AI 資源設定", L"AI resource settings"));
        const struct { int id; const wchar_t* chinese; const wchar_t* english; } labels[] = {
            {IDC_AI_INTRO, L"手動值是上限；實際配額仍依當下資源與系統安全預留縮小。設定可在 AI 關閉或未開圖時調整。", L"Manual values are ceilings. Effective budgets still shrink for current resources and safety reserves. Settings work with AI off or no image open."},
            {IDC_AI_AUTOMATIC, L"自動（預設）", L"Automatic (default)"}, {IDC_AI_MANUAL, L"手動上限", L"Manual limits"},
            {IDC_AI_RAM_LABEL, L"AI RAM 配額池上限（GiB，整數，至少 1）", L"AI RAM pool ceiling (GiB, whole number, at least 1)"},
            {IDC_AI_DISK_LABEL, L"AI 磁碟快取上限（GiB，整數，至少 1）", L"AI disk cache ceiling (GiB, whole number, at least 1)"},
            {IDC_AI_CPU_LABEL, L"AI 子程序 CPU 上限（%，1–100）", L"AI child CPU ceiling (%, 1–100)"},
            {IDC_AI_BATCH_LABEL, L"單批最多頁數（1–8）", L"Maximum pages per batch (1–8)"},
            {IDC_AI_GPU_LABEL, L"至少保留顯存（MiB，0 自動；其他至少 512）", L"GPU reserve (MiB, 0 = auto; otherwise at least 512)"},
            {IDC_AI_SCOPE, L"RAM 池由 AI 快取、顯示、工作與子程序共用，並非整個程式的硬性上限。CPU 低於 100% 時限制 AI 子程序及子孫；不含主程式解碼與顯示。", L"RAM is shared by AI cache, display, work and child; it is not an app-wide hard cap. CPU below 100% caps the AI child and descendants, excluding app decoding/display."},
            {IDC_AI_GPU_NOTE, L"顯存保留是暫停門檻，非硬性配額。無可靠 GPU 資訊且明確設定門檻時，AI 會暫停；0 自動則採保守單頁。", L"GPU reserve is a pause threshold, not a hard cap. With an explicit reserve and unverified GPU data, AI pauses; 0 = auto keeps conservative single-page work."},
            {IDC_AI_RESOURCES_LABEL, L"目前資源（單次讀取）", L"Current resources (one snapshot)"},
            {IDC_AI_EFFECTIVE_LABEL, L"預估安全配額（會隨資源變動）", L"Estimated safe budgets (vary with resource pressure)"},
            {IDC_AI_REFRESH, L"讀取目前資源", L"Refresh resources"}, {IDC_AI_RESET, L"重設為自動", L"Reset to automatic"},
            {IDOK, L"套用", L"Apply"}, {IDCANCEL, L"取消", L"Cancel"}
        };
        for (const auto& label : labels) SetDlgItemTextW(dialog, label.id, Local(*state, label.chinese, label.english));
        for (const auto id : Fields) SendDlgItemMessageW(dialog, id, EM_SETLIMITTEXT, 20, 0);
        Fill(dialog, *state, state->initial); Error(dialog, state->initial.error); Preview(dialog, *state);
        Center(dialog, GetWindow(dialog, GW_OWNER)); BeginProbe(dialog, *state);
        return TRUE;
    }
    if (!state) return FALSE;
    if (message == WM_CLOSE || (message == WM_COMMAND && LOWORD(wParam) == IDCANCEL)) {
        KillTimer(dialog, ProbeTimer); EndDialog(dialog, IDCANCEL); return TRUE;
    }
    if (message == WM_TIMER && wParam == ProbeTimer) {
        if (state->pending && state->pending->done.load(std::memory_order_acquire)) {
            const auto result = std::move(state->pending);
            state->snapshot = result->snapshot; state->sampled = true;
            KillTimer(dialog, ProbeTimer); EnableWindow(GetDlgItem(dialog, IDC_AI_REFRESH), TRUE);
            DisplaySnapshot(dialog, *state); Preview(dialog, *state);
            if (!result->error.empty()) Error(dialog, result->error);
        }
        return TRUE;
    }
    if (message == WM_COMMAND) {
        const auto id = LOWORD(wParam);
        if (id == IDOK) {
            UserLimits value; std::wstring error;
            if (!ReadInput(dialog, *state, value, error, true)) Error(dialog, error);
            else { state->result = std::move(value); KillTimer(dialog, ProbeTimer); EndDialog(dialog, IDOK); }
            return TRUE;
        }
        if (id == IDC_AI_REFRESH) { BeginProbe(dialog, *state); return TRUE; }
        if (id == IDC_AI_RESET) { Fill(dialog, *state, {}); Error(dialog, L""); Preview(dialog, *state); return TRUE; }
        if ((id == IDC_AI_AUTOMATIC || id == IDC_AI_MANUAL) && HIWORD(wParam) == BN_CLICKED) {
            EnableFields(dialog); Error(dialog, L""); Preview(dialog, *state); return TRUE;
        }
        if (HIWORD(wParam) == EN_CHANGE && !state->filling && std::find(std::begin(Fields), std::end(Fields), id) != std::end(Fields)) {
            Preview(dialog, *state); return TRUE;
        }
    }
    if (message == WM_DESTROY) { KillTimer(dialog, ProbeTimer); SetWindowLongPtrW(dialog, DWLP_USER, 0); }
    return FALSE;
}
}
std::optional<UserLimits> ShowResourceDialog(HWND owner, bool english, const UserLimits& initial,
    const std::filesystem::path& workRoot, ResourceProbe probe) {
    State state{english, initial, workRoot, probe ? std::move(probe) : ResourceProbe(ProbeResources)};
    DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_AI_RESOURCES), owner, Procedure, reinterpret_cast<LPARAM>(&state));
    return std::move(state.result);
}
}
