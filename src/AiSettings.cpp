#include "AiSettings.h"

#include <Windows.h>

#include <algorithm>
#include <cwctype>
#include <limits>
#include <map>
#include <vector>

namespace ai {
namespace {
constexpr std::uint64_t MiB = 1024ULL * 1024, GiB = 1024ULL * MiB;
constexpr auto Section = L"aiResources";

std::wstring Lower(std::wstring text) {
    std::transform(text.begin(), text.end(), text.begin(), [](wchar_t value) { return static_cast<wchar_t>(std::towlower(value)); });
    return text;
}

bool Number(const std::wstring& text, std::uint64_t& value) {
    if (text.empty()) return false;
    value = 0;
    for (const auto digit : text) {
        if (digit < L'0' || digit > L'9') return false;
        const auto next = static_cast<unsigned>(digit - L'0');
        if (value > (std::numeric_limits<std::uint64_t>::max() - next) / 10) return false;
        value = value * 10 + next;
    }
    return true;
}

bool ReadSection(const std::filesystem::path& path, const bool names, std::vector<wchar_t>& buffer) {
    for (std::size_t size = 256; size <= 1024 * 1024; size *= 2) {
        buffer.assign(size, L'\0');
        const auto count = names ? GetPrivateProfileSectionNamesW(buffer.data(), static_cast<DWORD>(size), path.c_str()) :
            GetPrivateProfileSectionW(Section, buffer.data(), static_cast<DWORD>(size), path.c_str());
        if (count < size - 2) return true;
    }
    return false;
}
}

bool ValidateUserLimits(const UserLimits& value, std::wstring& error) {
    error.clear();
    if (!value.error.empty()) error = value.error;
    else if (!value.manual) return true;
    else if (value.ramBytes < GiB || value.ramBytes % GiB || value.diskBytes < GiB || value.diskBytes % GiB)
        error = L"RAM 與快取上限須為至少 1 GiB 的整數。 / RAM and cache limits must be whole GiB, at least 1.";
    else if (value.cpuPercent < 1 || value.cpuPercent > 100)
        error = L"CPU 上限須為 1–100%。 / CPU limit must be 1–100%.";
    else if (value.batchPages < 1 || value.batchPages > 8)
        error = L"單批頁數須為 1–8。 / Batch size must be 1–8 pages.";
    else if (value.gpuReserveBytes && (value.gpuReserveBytes < 512 * MiB || value.gpuReserveBytes % MiB))
        error = L"顯存保留須為 0 或至少 512 MiB 的整數。 / GPU reserve must be 0 or whole MiB, at least 512.";
    return error.empty();
}

UserLimits ReadUserLimits(const std::filesystem::path& requested) {
    UserLimits result;
    std::error_code pathError;
    const auto path = std::filesystem::absolute(requested, pathError);
    const auto invalid = [&]() {
        result.error = L"AI 資源設定無效或不完整，請開啟資源設定修正或重設。 / AI resource settings are invalid or incomplete; correct or reset them in Resource settings.";
        return result;
    };
    if (pathError || path.empty()) return invalid();
    const auto attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        const auto error = GetLastError();
        return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND ? result : invalid();
    }
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return invalid();
    CloseHandle(file);
    std::vector<wchar_t> buffer;
    if (!ReadSection(path, true, buffer)) return invalid();
    bool present = false;
    for (const wchar_t* entry = buffer.data(); *entry; entry += std::wcslen(entry) + 1) {
        if (Lower(entry) == L"airesources") {
            if (present) return invalid();
            present = true;
        }
    }
    if (!present) return result;
    if (!ReadSection(path, false, buffer)) return invalid();
    std::map<std::wstring, std::wstring> values;
    bool duplicate = false;
    for (const wchar_t* entry = buffer.data(); *entry; entry += std::wcslen(entry) + 1) {
        const std::wstring text(entry);
        const auto separator = text.find(L'=');
        if (separator == std::wstring::npos) return invalid();
        const auto key = Lower(text.substr(0, separator));
        if (!values.emplace(key, text.substr(separator + 1)).second) {
            if (key == L"mode") return invalid();
            duplicate = true;
        }
    }
    const auto mode = values.find(L"mode");
    if (mode == values.end()) return invalid();
    if (mode->second == L"automatic") return result;
    result.manual = true;
    if (mode->second != L"manual" || duplicate) return invalid();
    const auto read = [&](const wchar_t* key, const std::uint64_t unit, std::uint64_t& target) {
        const auto entry = values.find(key);
        std::uint64_t number = 0;
        if (entry == values.end() || !Number(entry->second, number) || number > std::numeric_limits<std::uint64_t>::max() / unit) return false;
        target = number * unit; return true;
    };
    std::uint64_t cpu = 0, batch = 0;
    if (!read(L"ramgib", GiB, result.ramBytes) || !read(L"diskgib", GiB, result.diskBytes) ||
        !read(L"gpureservemib", MiB, result.gpuReserveBytes) || !read(L"cpupercent", 1, cpu) ||
        !read(L"batchpages", 1, batch) || cpu > 100 || batch > 8) return invalid();
    result.cpuPercent = static_cast<unsigned>(cpu); result.batchPages = static_cast<unsigned>(batch);
    std::wstring error;
    return ValidateUserLimits(result, error) ? result : invalid();
}

bool WriteUserLimits(const std::filesystem::path& requested, const UserLimits& value, std::wstring& error) {
    if (!ValidateUserLimits(value, error)) return false;
    std::error_code pathError;
    const auto path = std::filesystem::absolute(requested, pathError);
    if (pathError) { error = L"無法儲存 AI 資源設定。 / Could not save AI resource settings."; return false; }
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path(), pathError);
    if (pathError) { error = L"無法建立 AI 設定資料夾。 / Could not create the AI settings directory."; return false; }
    std::wstring section;
    const auto add = [&](const wchar_t* key, const std::wstring& text) { section += key; section += L'='; section += text; section += L'\0'; };
    add(L"mode", value.manual ? L"manual" : L"automatic");
    if (value.manual) {
        add(L"ramGiB", std::to_wstring(value.ramBytes / GiB));
        add(L"diskGiB", std::to_wstring(value.diskBytes / GiB));
        add(L"cpuPercent", std::to_wstring(value.cpuPercent));
        add(L"batchPages", std::to_wstring(value.batchPages));
        add(L"gpuReserveMiB", std::to_wstring(value.gpuReserveBytes / MiB));
    }
    section += L'\0';
    if (!WritePrivateProfileSectionW(Section, section.c_str(), path.c_str())) {
        error = L"無法儲存 AI 資源設定。 / Could not save AI resource settings."; return false;
    }
    return true;
}
}
