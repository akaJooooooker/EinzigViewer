#pragma once

#include "AiSettings.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

namespace ai {

struct ResourceSnapshot {
    std::uint64_t totalPhysical = 0, availablePhysical = 0, availableCommit = 0;
    std::uint64_t diskTotal = 0, diskAvailable = 0, gpuTotal = 0, gpuAvailable = 0;
    // DXGI values describe only this parent process, never global/child VRAM.
    std::uint64_t displayBudget = 0, displayUsage = 0;
    bool memoryKnown = false, diskKnown = false, gpuKnown = false;
};

struct ResourceLimits {
    std::uint64_t totalBytes = 0, cacheBytes = 0, displayBytes = 0, outputBytes = 0;
    std::uint64_t workBytes = 0, transientBytes = 0, jobBytes = 0, diskBytes = 0, stagingBytes = 0;
    unsigned batchPages = 1, cpuPercent = 100;
    bool allowed = false, gpuKnown = false;
    std::wstring reason;
};

using ResourceProbe = std::function<ResourceSnapshot(const std::filesystem::path&, int)>;

ResourceSnapshot ProbeResources(const std::filesystem::path& directory, int gpu);
// Zero estimates prospective capacity; pass the concrete batch's final output
// bytes before work so disk staging is reserved and the batch bound is checked.
ResourceLimits ComputeLimits(const ResourceSnapshot&, std::uint64_t batchOutputBytes = 0, const UserLimits& = {});
bool ResourcePressure(const ResourceSnapshot&, std::wstring& reason, const UserLimits& = {});
// One healthy sample, including recovery margin; the scheduler counts three samples.
bool ResourcesRecovered(const ResourceSnapshot&, const UserLimits& = {});

} // namespace ai
