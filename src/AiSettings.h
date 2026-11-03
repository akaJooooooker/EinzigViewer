#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace ai {
struct UserLimits {
    bool manual = false;
    std::uint64_t ramBytes = 4ULL * 1024 * 1024 * 1024;
    std::uint64_t diskBytes = 4ULL * 1024 * 1024 * 1024;
    std::uint64_t gpuReserveBytes = 0;
    unsigned cpuPercent = 50, batchPages = 2;
    std::wstring error;
};

bool ValidateUserLimits(const UserLimits&, std::wstring&);
UserLimits ReadUserLimits(const std::filesystem::path&);
bool WriteUserLimits(const std::filesystem::path&, const UserLimits&, std::wstring&);
}
