#pragma once

#include <atomic>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

// Owns only a uniquely-created extraction directory. Decoding tasks retain this
// object so opening another book cannot delete files still being read.
class EpubBook {
public:
    ~EpubBook();
    static std::shared_ptr<EpubBook> Open(const std::filesystem::path& path,
        const std::atomic_bool& cancelled, std::wstring& error);
    std::filesystem::path source;
    std::vector<std::filesystem::path> pages;
    std::vector<std::wstring> labels;
private:
    std::filesystem::path extractionDirectory_;
};
