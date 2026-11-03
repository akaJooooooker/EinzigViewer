#include "UpdateChecker.h"

#include <commctrl.h>
#include <winhttp.h>
#include <roapi.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <array>
#include <chrono>
#include <optional>
#include <thread>

namespace updates {
namespace {
constexpr wchar_t ReleaseBase[] = L"https://github.com/akaJooooooker/EinzigViewer/releases/";
constexpr std::size_t MaximumResponse = 1024 * 1024;

std::optional<std::array<unsigned, 3>> Version(std::wstring_view text) {
    std::array<unsigned, 3> result{};
    for (unsigned i = 0; i < 3; ++i) {
        const auto dot = text.find(L'.');
        const auto part = text.substr(0, dot);
        if (part.empty() || part.size() > 5 || (part.size() > 1 && part.front() == L'0')) return {};
        for (const wchar_t digit : part) {
            if (digit < L'0' || digit > L'9') return {};
            result[i] = result[i] * 10 + static_cast<unsigned>(digit - L'0');
        }
        if (result[i] > 65535) return {};
        if (i == 2) { if (dot != text.npos) return {}; }
        else { if (dot == text.npos) return {}; text.remove_prefix(dot + 1); }
    }
    return result;
}

struct HttpHandle {
    HINTERNET value = nullptr;
    explicit HttpHandle(HINTERNET handle) : value(handle) {}
    ~HttpHandle() { if (value) WinHttpCloseHandle(value); }
    HttpHandle(const HttpHandle&) = delete;
    HttpHandle& operator=(const HttpHandle&) = delete;
};

std::optional<std::string> FetchJson() {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    HttpHandle session(WinHttpOpen(L"EinzigViewer update checker", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session.value || !WinHttpSetTimeouts(session.value, 5000, 5000, 5000, 5000)) return {};
    HttpHandle connection(WinHttpConnect(session.value, L"api.github.com", INTERNET_DEFAULT_HTTPS_PORT, 0));
    if (!connection.value) return {};
    HttpHandle request(WinHttpOpenRequest(connection.value, L"GET",
        L"/repos/akaJooooooker/EinzigViewer/releases/latest", nullptr, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
    if (!request.value) return {};
    DWORD redirects = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    if (!WinHttpSetOption(request.value, WINHTTP_OPTION_REDIRECT_POLICY, &redirects, sizeof(redirects))) return {};
    // Keep system TLS validation; no credentials, images, or image paths are sent.
    constexpr wchar_t headers[] = L"Accept: application/vnd.github+json\r\n";
    if (!WinHttpSendRequest(request.value, headers, static_cast<DWORD>(-1L), WINHTTP_NO_REQUEST_DATA,
            0, 0, 0) || !WinHttpReceiveResponse(request.value, nullptr)) return {};
    DWORD status = 0, size = sizeof(status);
    if (!WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX) || status != 200) return {};
    std::string body;
    std::array<char, 8192> buffer{};
    for (;;) {
        if (std::chrono::steady_clock::now() >= deadline) return {};
        DWORD read = 0;
        if (!WinHttpReadData(request.value, buffer.data(), static_cast<DWORD>(buffer.size()), &read)) return {};
        if (!read) break;
        if (body.size() + read > MaximumResponse) return {};
        body.append(buffer.data(), read);
    }
    return body;
}
}

Result EvaluateRelease(const std::string_view json, const std::wstring_view currentVersion, const Edition edition) {
    try {
        if (json.size() > MaximumResponse) return {};
        // Windows.Data.Json is provided by Windows 10; no bundled JSON runtime.
        using namespace winrt::Windows::Data::Json;
        JsonObject root;
        if (!JsonObject::TryParse(winrt::to_hstring(json), root) ||
            root.GetNamedBoolean(L"draft") || root.GetNamedBoolean(L"prerelease")) return {};
        const std::wstring tag(root.GetNamedString(L"tag_name"));
        if (tag.empty() || tag.front() != L'v') return {};
        const std::wstring version = tag.substr(1);
        const auto parsed = Version(version), current = Version(currentVersion);
        if (!parsed || !current) return {};
        Result result{*parsed > *current ? Status::Available : Status::Current, version,
            std::wstring(ReleaseBase) + L"tag/" + tag};
        const std::wstring assetName = L"EinzigViewer-" + version + L"-win-x64" +
            (edition == Edition::Installed ? L"-Setup.exe" : L".zip");
        const std::wstring expectedUrl = std::wstring(ReleaseBase) + L"download/" + tag + L"/" + assetName;
        for (const auto& value : root.GetNamedArray(L"assets", JsonArray{})) {
            if (value.ValueType() != JsonValueType::Object) continue;
            const auto asset = value.GetObject();
            if (asset.GetNamedString(L"name", L"") == assetName &&
                asset.GetNamedString(L"state", L"") == L"uploaded" &&
                asset.GetNamedString(L"browser_download_url", L"") == expectedUrl) {
                result.downloadUrl = expectedUrl;
                break;
            }
        }
        return result;
    } catch (...) { return {}; }
}

bool ShouldCheckAutomatically(const bool enabled, const std::int64_t lastCheck, const std::int64_t now) {
    return enabled && (lastCheck <= 0 || now < lastCheck || now - lastCheck >= 24 * 60 * 60);
}
bool ShouldNotify(const Result& result, const bool manual, const std::wstring_view skippedVersion) {
    return result.status == Status::Available && (manual || result.version != skippedVersion);
}
std::filesystem::path SettingsFile(const std::filesystem::path& executableDirectory,
    const std::filesystem::path& localAppData, const Edition edition) {
    return edition == Edition::Portable ? executableDirectory / L"settings.ini" :
        (localAppData.empty() ? std::filesystem::path{} : localAppData / L"EinzigViewer" / L"settings.ini");
}
Edition DetectEdition(const std::filesystem::path& executableDirectory) {
    std::error_code error;
    return std::filesystem::is_regular_file(executableDirectory / L"portable.ini", error)
        ? Edition::Portable : Edition::Installed;
}
std::filesystem::path ExecutableDirectory() {
    std::wstring path(32768, L'\0');
    const DWORD count = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (count == 0 || count >= path.size()) return {};
    path.resize(count);
    return std::filesystem::path(path).parent_path();
}
Result FetchLatest(const Edition edition, const std::wstring_view currentVersion) {
    try {
        const HRESULT initialized = RoInitialize(RO_INIT_MULTITHREADED);
        struct Apartment { HRESULT value; ~Apartment() {
            if (SUCCEEDED(value)) { winrt::clear_factory_cache(); RoUninitialize(); }
        } } apartment{initialized};
        if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE) return {};
        const auto json = FetchJson();
        return json ? EvaluateRelease(*json, currentVersion, edition) : Result{};
    } catch (...) { return {}; }
}
std::shared_ptr<Job> StartCheck(const Edition edition, std::wstring currentVersion) {
    auto job = std::make_shared<Job>();
    std::thread([job, edition, version = std::move(currentVersion)] {
        job->result = FetchLatest(edition, version);
        job->done.store(true, std::memory_order_release);
    }).detach();
    return job;
}

Action ShowUpdateDialog(const HWND owner, const bool english, const Result& release,
    const std::wstring_view currentVersion, const Edition edition) {
    const std::wstring title = english ? L"EinzigViewer update" : L"EinzigViewer 更新";
    const std::wstring instruction = (english ? L"A new version is available: " : L"發現新版本：") + release.version;
    const std::wstring content = (english ? L"Current version: " : L"目前版本：") + std::wstring(currentVersion) +
        (edition == Edition::Installed
            ? (english ? L"\nDownload the installer, close EinzigViewer, then install the update. Your settings are kept."
                       : L"\n下載安裝版後，關閉 EinzigViewer 再安裝更新，原有設定會保留。")
            : (english ? L"\nDownload the portable ZIP, close EinzigViewer, then extract it over the old folder. Keep settings.ini."
                       : L"\n下載免安裝 ZIP 後，關閉 EinzigViewer 再解壓縮覆蓋舊資料夾，保留 settings.ini。"));
    const TASKDIALOG_BUTTON buttons[]{
        {static_cast<int>(Action::Download), english ? L"Download" : L"下載"},
        {static_cast<int>(Action::Later), english ? L"Later" : L"稍後"},
        {static_cast<int>(Action::Skip), english ? L"Skip this version" : L"略過此版本"}};
    TASKDIALOGCONFIG config{sizeof(config)};
    config.hwndParent = owner;
    config.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_SIZE_TO_CONTENT;
    config.pszWindowTitle = title.c_str();
    config.pszMainIcon = TD_INFORMATION_ICON;
    config.pszMainInstruction = instruction.c_str();
    config.pszContent = content.c_str();
    config.cButtons = static_cast<UINT>(std::size(buttons));
    config.pButtons = buttons;
    config.nDefaultButton = static_cast<int>(Action::Later);
    int pressed = static_cast<int>(Action::Later);
    if (FAILED(TaskDialogIndirect(&config, &pressed, nullptr, nullptr))) return Action::Later;
    return pressed == static_cast<int>(Action::Download) ? Action::Download :
        pressed == static_cast<int>(Action::Skip) ? Action::Skip : Action::Later;
}
}
