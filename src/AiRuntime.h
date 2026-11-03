#pragma once

#include "ImageData.h"

#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ai {
struct RuntimeOptions {
    std::filesystem::path executable;
    std::filesystem::path modelDirectory;
    unsigned timeoutMs = 120000;
    unsigned cpuPercent = 100; // 1..99: Job hard cap, 100: below-normal priority without a cap.
    unsigned tileSize = 128;
    int gpu = 0;
    std::uint64_t maximumOutputBytes = 256ULL * 1024 * 1024;
    std::uint64_t maximumBatchBytes = 256ULL * 1024 * 1024;
    std::uint64_t jobMemoryBytes = 2ULL * 1024 * 1024 * 1024;
    std::uint64_t stagingBytes = 768ULL * 1024 * 1024;
    unsigned maximumDimension = 16384;
    std::function<std::wstring()> stopReason;
};

struct Artifact {
    std::filesystem::path png;
    std::shared_ptr<void> lease;
};

struct Output {
    std::shared_ptr<ImageData> image;
    Artifact artifact;
};

struct BatchInput { std::wstring key; std::shared_ptr<ImageData> image; };
using Completed = std::function<void(std::size_t, Output)>;
using BatchExecutor = std::function<void(const RuntimeOptions&,
    const std::vector<BatchInput>&, int, const std::filesystem::path&,
    const std::atomic_bool&, const Completed&, std::wstring&)>;

bool ValidateOutputSize(const ImageData& source, int scale, std::wstring& error);
bool ValidateOutputSize(const ImageData& source, int scale, std::wstring& error,
    std::uint64_t maximumBytes, unsigned maximumDimension = 16384);
void RunBatch(const RuntimeOptions&, const std::vector<BatchInput>&, int,
    const std::filesystem::path&, const std::atomic_bool&, const Completed&, std::wstring&);
Output Run(const RuntimeOptions& options, const ImageData& source, int scale,
    const std::filesystem::path& workRoot, const std::atomic_bool& cancelled,
    std::wstring& error);
}
