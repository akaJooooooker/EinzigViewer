#include "App.h"
#include "UpdateChecker.h"
#include <commctrl.h>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void Require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
updates::Action requestedAction;
LRESULT CALLBACK DialogHook(int code, WPARAM wParam, LPARAM lParam) {
    if (code == HCBT_ACTIVATE) {
        const HWND window = reinterpret_cast<HWND>(wParam);
        wchar_t title[128]{}; GetWindowTextW(window, title, 128);
        if (std::wstring_view(title) == L"EinzigViewer update" || std::wstring_view(title) == L"EinzigViewer 更新")
            PostMessageW(window, TDM_CLICK_BUTTON, static_cast<WPARAM>(requestedAction), 0);
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}
}
struct UpdateUiTest {
    static void Run() {
        const auto testDirectory = updates::ExecutableDirectory();
        Require(testDirectory.parent_path().filename() == L"update-ui-probe", "run only from isolated UI test directory");
        std::filesystem::remove(testDirectory / L"settings.ini");
        App app;
        Require(app.CreateDeviceIndependentResources() && app.CreateMainWindow(GetModuleHandleW(nullptr)), "create viewer");
        struct Scope { App& app; ~Scope() { if (IsWindow(app.window_)) DestroyWindow(app.window_); } } scope{app};
        app.LoadSettings();
        app.mainMenu_ = app.CreateApplicationMenu(); SetMenu(app.window_, app.mainMenu_);
        Require(app.edition_ == updates::Edition::Portable, "portable marker detected");
        App stale;
        stale.LoadSettings();
        app.automaticUpdates_ = false;
        app.lastUpdateCheck_ = 123456;
        app.skippedUpdate_ = L"0.9.0";
        app.SaveUpdateSettings();
        stale.SaveSettings();
        app.automaticUpdates_ = true; app.lastUpdateCheck_ = 0; app.skippedUpdate_.clear();
        app.LoadSettings();
        Require(!app.automaticUpdates_ && app.lastUpdateCheck_ == 123456 && app.skippedUpdate_ == L"0.9.0", "update choices survive restart");
        app.StartUpdateCheck(false);
        Require(!app.updateJob_, "disabled automatic check starts no network job");

        const updates::Result release{updates::Status::Available, L"9.0.0",
            L"https://github.com/akaJooooooker/EinzigViewer/releases/tag/v9.0.0"};
        app.updateJob_ = std::make_shared<updates::Job>();
        app.updateJob_->result = release; app.updateJob_->done.store(true);
        app.updateManual_ = false;
        app.PollUpdateCheck();
        Require(!app.updateJob_, "disabling automatic checks also suppresses an in-flight completion");
        const HHOOK hook = SetWindowsHookExW(WH_CBT, DialogHook, nullptr, GetCurrentThreadId());
        Require(hook != nullptr, "own-thread dialog hook");
        for (const bool english : {false, true}) {
            for (const auto edition : {updates::Edition::Portable, updates::Edition::Installed}) {
                for (const auto action : {updates::Action::Download, updates::Action::Later, updates::Action::Skip}) {
                    requestedAction = action;
                    Require(updates::ShowUpdateDialog(app.window_, english, release, L"0.8.0", edition) == action,
                        "native update dialog returns clicked action");
                }
            }
        }
        // Drive App's real completion path with a completed response, without
        // making an HTTP call or opening the browser during the test.
        requestedAction = updates::Action::Skip;
        app.updateJob_ = std::make_shared<updates::Job>();
        app.updateJob_->result = release; app.updateJob_->done.store(true);
        app.updateManual_ = true;
        app.PollUpdateCheck();
        Require(!app.updateJob_ && app.skippedUpdate_ == L"9.0.0", "popup skip is handled and persisted by App");
        UnhookWindowsHookEx(hook);
        // Never block a viewer's close on a worker that has not finished.
        app.updateJob_ = std::make_shared<updates::Job>();
        const auto before = std::chrono::steady_clock::now();
        DestroyWindow(app.window_);
        Require(std::chrono::steady_clock::now() - before < std::chrono::seconds(2), "shutdown does not wait for update worker");
        std::filesystem::remove(app.SettingsPath());
        std::cout << "PASS: bilingual popup actions for both editions, preference persistence, skip completion, nonblocking close.\n";
    }
};
int main() {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    int result = 0;
    try { UpdateUiTest::Run(); }
    catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; result = 1; }
    CoUninitialize(); return result;
}
