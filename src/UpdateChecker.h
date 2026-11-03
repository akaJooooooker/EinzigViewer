#pragma once

#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace updates {
enum class Edition { Portable, Installed };
enum class Status { Failed, Current, Available };
struct Result {
    Status status = Status::Failed;
    std::wstring version;
    std::wstring downloadUrl;
};
enum class Action : int { Later = 1002, Download = 1001, Skip = 1003 };

Result EvaluateRelease(std::string_view json, std::wstring_view currentVersion, Edition edition);
bool ShouldCheckAutomatically(bool enabled, std::int64_t lastCheck, std::int64_t now);
bool ShouldNotify(const Result& result, bool manual, std::wstring_view skippedVersion);
std::filesystem::path SettingsFile(const std::filesystem::path& executableDirectory,
    const std::filesystem::path& localAppData, Edition edition);
Edition DetectEdition(const std::filesystem::path& executableDirectory);
std::filesystem::path ExecutableDirectory();
Result FetchLatest(Edition edition, std::wstring_view currentVersion);

// Worker owns only this shared result, never App or HWND. Completion publishes it
// with release/acquire ordering; closing a viewer never waits for the network.
struct Job {
    std::atomic_bool done{false};
    Result result;
};
std::shared_ptr<Job> StartCheck(Edition edition, std::wstring currentVersion);
Action ShowUpdateDialog(HWND owner, bool english, const Result& release,
    std::wstring_view currentVersion, Edition edition);
}
