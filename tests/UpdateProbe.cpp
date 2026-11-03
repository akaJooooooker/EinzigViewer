#include "UpdateChecker.h"
#include <Windows.h>
#include <roapi.h>
#include <iostream>
#include <stdexcept>

namespace {
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
const std::string Release = R"({
 "tag_name":"v0.10.0","draft":false,"prerelease":false,
 "html_url":"https://github.com/akaJooooooker/EinzigViewer/releases/tag/v0.10.0",
 "assets":[
 {"name":"EinzigViewer-0.10.0-win-x64-Setup.exe","state":"uploaded",
 "browser_download_url":"https://github.com/akaJooooooker/EinzigViewer/releases/download/v0.10.0/EinzigViewer-0.10.0-win-x64-Setup.exe"},
 {"name":"EinzigViewer-0.10.0-win-x64.zip","state":"uploaded",
 "browser_download_url":"https://github.com/akaJooooooker/EinzigViewer/releases/download/v0.10.0/EinzigViewer-0.10.0-win-x64.zip"}]})";
}
int main(int argc, char**) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    std::cerr << "UpdateProbe: starting release validation\n";
    RoInitialize(RO_INIT_MULTITHREADED);
    struct Apartment { ~Apartment() { RoUninitialize(); } } apartment;
    try {
        using namespace updates;
        const auto portable = EvaluateRelease(Release, L"0.9.0", Edition::Portable);
        std::cerr << "UpdateProbe: first release evaluated\n";
        Require(portable.status == Status::Available && portable.version == L"0.10.0",
            "numeric version comparison: 0.10 is newer than 0.9");
        Require(portable.downloadUrl == L"https://github.com/akaJooooooker/EinzigViewer/releases/download/v0.10.0/EinzigViewer-0.10.0-win-x64.zip", "portable ZIP link");
        const auto installed = EvaluateRelease(Release, L"0.9.0", Edition::Installed);
        Require(installed.downloadUrl == L"https://github.com/akaJooooooker/EinzigViewer/releases/download/v0.10.0/EinzigViewer-0.10.0-win-x64-Setup.exe", "installer link");
        Require(EvaluateRelease(Release, L"0.10.0", Edition::Portable).status == Status::Current, "equal version is current");
        Require(EvaluateRelease(Release, L"1.0.0", Edition::Portable).status == Status::Current, "do not offer downgrade");
        for (const std::string body : {"{}", "not json", "[]",
            R"({"tag_name":"v0.11.0-beta","draft":false,"prerelease":false})",
            R"({"tag_name":"v999999999999.0.0","draft":false,"prerelease":false})",
            R"({"tag_name":"v0.11.0","draft":true,"prerelease":false})",
            R"({"tag_name":"v0.11.0","draft":false,"prerelease":true})"}) {
            Require(EvaluateRelease(body, L"0.9.0", Edition::Portable).status == Status::Failed, "reject malformed or unstable release");
        }
        auto unsafe = Release;
        const std::string originalUrl = "https://github.com/akaJooooooker/EinzigViewer/releases/download/v0.10.0/EinzigViewer-0.10.0-win-x64.zip";
        const auto offset = unsafe.find(originalUrl);
        unsafe.replace(offset, originalUrl.size(), "https://attacker.invalid/payload.zip");
        Require(EvaluateRelease(unsafe, L"0.9.0", Edition::Portable).downloadUrl ==
            L"https://github.com/akaJooooooker/EinzigViewer/releases/tag/v0.10.0", "unsafe asset falls back to trusted release page");
        const auto absent = EvaluateRelease(R"({"tag_name":"v0.11.0","draft":false,"prerelease":false,"assets":[]})", L"0.9.0", Edition::Installed);
        Require(absent.status == Status::Available && absent.downloadUrl == L"https://github.com/akaJooooooker/EinzigViewer/releases/tag/v0.11.0", "missing edition asset falls back to release page");
        Require(ShouldCheckAutomatically(true, 0, 100000), "first launch checks");
        Require(!ShouldCheckAutomatically(false, 0, 100000), "automatic toggle disables check");
        Require(!ShouldCheckAutomatically(true, 99999, 100000), "cooldown survives restart");
        Require(ShouldCheckAutomatically(true, 13600, 100000), "check after exactly 24 hours");
        Require(ShouldCheckAutomatically(true, 110000, 100000), "clock correction does not disable checks forever");
        Require(!ShouldNotify(portable, false, L"0.10.0"), "skipped version stays quiet");
        Require(ShouldNotify(portable, true, L"0.10.0"), "manual check overrides skip");
        Require(SettingsFile(L"C:/portable", L"C:/local", Edition::Portable) == std::filesystem::path(L"C:/portable/settings.ini"), "portable settings follow directory");
        Require(SettingsFile(L"C:/installed", L"C:/local", Edition::Installed) == std::filesystem::path(L"C:/local/EinzigViewer/settings.ini"), "installed settings use local app data");
        if (argc > 1) {
            const auto live = FetchLatest(Edition::Portable, L"0.0.0");
            Require(live.status == Status::Available, "live GitHub HTTPS request and release parsing");
            std::wcout << L"LIVE: " << live.version << L" " << live.downloadUrl << L'\n';
        }
        std::cout << "PASS: release versions, edition downloads, invalid data, trusted links, cooldown, skip, settings.\n";
    } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
