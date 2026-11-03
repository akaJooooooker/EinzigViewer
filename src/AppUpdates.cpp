#include "App.h"
#include "AppVersion.h"
#include <shellapi.h>
#include <chrono>

void App::SaveUpdateSettings(const UpdateField field) const {
    const auto path = SettingsPath();
    if (path.empty()) return;
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    const auto last = std::to_wstring(lastUpdateCheck_);
    if (field == UpdateField::All || field == UpdateField::Automatic)
        WritePrivateProfileStringW(L"updates", L"automatic", automaticUpdates_ ? L"1" : L"0", path.c_str());
    if (field == UpdateField::All || field == UpdateField::LastCheck)
        WritePrivateProfileStringW(L"updates", L"lastCheck", last.c_str(), path.c_str());
    if (field == UpdateField::All || field == UpdateField::SkippedVersion)
        WritePrivateProfileStringW(L"updates", L"skippedVersion", skippedUpdate_.c_str(), path.c_str());
}

void App::LoadUpdateSettings() {
    const auto path = SettingsPath();
    if (path.empty()) return;
    automaticUpdates_ = GetPrivateProfileIntW(L"updates", L"automatic", 1, path.c_str()) != 0;
    wchar_t text[64]{};
    GetPrivateProfileStringW(L"updates", L"lastCheck", L"0", text, 64, path.c_str());
    lastUpdateCheck_ = _wcstoi64(text, nullptr, 10);
    GetPrivateProfileStringW(L"updates", L"skippedVersion", L"", text, 64, path.c_str());
    skippedUpdate_ = text;
}

void App::StartUpdateCheck(const bool manual) {
    if (!manual) { LoadUpdateSettings(); UpdateMenuChecks(); }
    if (updateJob_) {
        if (manual) updateManual_ = true;
        return;
    }
    const auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    if (!manual && !updates::ShouldCheckAutomatically(automaticUpdates_, lastUpdateCheck_, now)) return;
    try {
        updateJob_ = updates::StartCheck(edition_, EINZIGVIEWER_VERSION_W);
    } catch (...) {
        if (manual) MessageBoxW(window_, IsEnglish() ? L"Could not start the update check. Please try again later."
            : L"無法開始檢查更新，請稍後再試。", L"EinzigViewer", MB_OK | MB_ICONINFORMATION);
        return;
    }
    updateManual_ = manual;
    lastUpdateCheck_ = now;
    SaveUpdateSettings(UpdateField::LastCheck);
    SetTimer(window_, 7, 200, nullptr);
    if (manual) SetNotice(IsEnglish() ? L"Checking for updates…" : L"正在檢查更新…");
}

void App::PollUpdateCheck() {
    if (!updateJob_ || !updateJob_->done.load(std::memory_order_acquire)) return;
    if (!updateManual_) {
        LoadUpdateSettings();
        UpdateMenuChecks();
        if (!automaticUpdates_) { updateJob_.reset(); KillTimer(window_, 7); return; }
    }
    const auto result = updateJob_->result;
    const bool notify = updates::ShouldNotify(result, updateManual_, skippedUpdate_);
    // Show automatic dialogs when the viewer is active; don't interrupt another
    // program. A completed result remains ready until the user returns.
    if (notify && !updateManual_ && GetForegroundWindow() != window_) return;
    const bool manual = updateManual_;
    updateJob_.reset();
    KillTimer(window_, 7);
    if (notify) {
        ShowFullscreenCursor();
        const auto action = updates::ShowUpdateDialog(window_, IsEnglish(), result,
            EINZIGVIEWER_VERSION_W, edition_);
        if (action == updates::Action::Skip) {
            skippedUpdate_ = result.version;
            SaveUpdateSettings(UpdateField::SkippedVersion);
        } else if (action == updates::Action::Download) {
            const auto opened = reinterpret_cast<INT_PTR>(ShellExecuteW(window_, L"open",
                result.downloadUrl.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
            if (opened <= 32) MessageBoxW(window_, IsEnglish()
                ? L"Could not open the browser. Visit github.com/akaJooooooker/EinzigViewer/releases."
                : L"無法開啟瀏覽器，請前往 github.com/akaJooooooker/EinzigViewer/releases 下載。",
                L"EinzigViewer", MB_OK | MB_ICONINFORMATION);
        }
        if (fullscreen_) ArmFullscreenCursorHide();
    } else if (manual) {
        MessageBoxW(window_, result.status == updates::Status::Current
            ? (IsEnglish() ? L"You are using the latest version." : L"目前已是最新版本。")
            : (IsEnglish() ? L"Could not check for updates. Check your internet connection and try again later."
                          : L"無法檢查更新，請確認網路連線或稍後再試。"),
            IsEnglish() ? L"EinzigViewer update" : L"EinzigViewer 更新", MB_OK | MB_ICONINFORMATION);
    }
}
