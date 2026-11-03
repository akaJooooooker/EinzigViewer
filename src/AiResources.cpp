#include "AiResources.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <limits>

namespace ai {
namespace {
constexpr std::uint64_t MiB = 1024ULL * 1024;
constexpr std::uint64_t GiB = 1024ULL * MiB;

std::uint64_t Subtract(std::uint64_t left, std::uint64_t right) {
    return left > right ? left - right : 0;
}

std::uint64_t Add(std::uint64_t left, std::uint64_t right) {
    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    return right > maximum - left ? maximum : left + right;
}

std::uint64_t Percent(std::uint64_t value, unsigned percent) {
    // Dividing first keeps every intermediate within uint64 even for UINT64_MAX.
    return (value / 100) * percent + ((value % 100) * percent) / 100;
}

std::uint64_t PhysicalReserve(const ResourceSnapshot& sample) {
    return std::max(2 * GiB, Percent(sample.totalPhysical, 15));
}

std::uint64_t DiskReserve(const ResourceSnapshot& sample) {
    return std::min(20 * GiB, std::max(5 * GiB, Percent(sample.diskTotal, 5)));
}

std::uint64_t GpuReserve(const ResourceSnapshot& sample, const UserLimits& user) {
    return std::max({512 * MiB, Percent(sample.gpuTotal, 10), user.manual ? user.gpuReserveBytes : 0});
}

bool ValidMemory(const ResourceSnapshot& sample) {
    return sample.memoryKnown && sample.totalPhysical != 0 && sample.availablePhysical <= sample.totalPhysical;
}

bool ValidDisk(const ResourceSnapshot& sample) {
    return sample.diskKnown && sample.diskTotal != 0 && sample.diskAvailable <= sample.diskTotal;
}

bool ValidGpu(const ResourceSnapshot& sample) {
    return !sample.gpuKnown || (sample.gpuTotal != 0 && sample.gpuAvailable <= sample.gpuTotal);
}

void ProbeDisk(const std::filesystem::path& requested, ResourceSnapshot& sample) {
    // A GUID lease may not exist yet. Walk to its existing ancestor on the same
    // volume, without creating directories or substituting another volume.
    std::error_code error;
    auto directory = std::filesystem::absolute(requested.empty() ? std::filesystem::path(L".") : requested, error);
    if (error) return;
    while (!directory.empty()) {
        const auto status = std::filesystem::status(directory, error);
        if (!error && std::filesystem::is_directory(status)) break;
        if (error && error != std::errc::no_such_file_or_directory && error != std::errc::not_a_directory) return;
        error.clear();
        const auto parent = directory.parent_path();
        if (parent == directory) return;
        directory = parent;
    }
    if (directory.empty()) return;
    ULARGE_INTEGER available{}, total{}, free{};
    if (!GetDiskFreeSpaceExW(directory.c_str(), &available, &total, &free)) return;
    // lpFreeBytesAvailable respects the current user's quota; free does not.
    sample.diskTotal = total.QuadPart;
    sample.diskAvailable = available.QuadPart;
    sample.diskKnown = sample.diskTotal != 0 && sample.diskAvailable <= sample.diskTotal;
}

#if defined(_WIN64)
// The small, private C ABI surface is described by Vulkan-Headers v1.1.130:
// https://github.com/KhronosGroup/Vulkan-Headers/blob/v1.1.130/include/vulkan/vulkan_core.h
// and NVIDIA's installed-driver NVML query ABI:
// https://docs.nvidia.com/deploy/nvml-api/latest/api/group__nvmlDeviceQueries.html
// Scalar aliases and consecutive same-type fields are grouped below; no Vulkan
// SDK, import library, GPU logical device, command queue or inference is needed.
namespace driver {
using U32 = std::uint32_t;
using U64 = std::uint64_t;
struct Limits {
    U32 maxImageDimension1D, maxImageDimension2D, maxImageDimension3D, maxImageDimensionCube;
    U32 maxImageArrayLayers, maxTexelBufferElements, maxUniformBufferRange, maxStorageBufferRange;
    U32 maxPushConstantsSize, maxMemoryAllocationCount, maxSamplerAllocationCount;
    U64 bufferImageGranularity, sparseAddressSpaceSize;
    U32 maxBoundDescriptorSets, maxPerStageDescriptorSamplers, maxPerStageDescriptorUniformBuffers;
    U32 maxPerStageDescriptorStorageBuffers, maxPerStageDescriptorSampledImages, maxPerStageDescriptorStorageImages;
    U32 maxPerStageDescriptorInputAttachments, maxPerStageResources, maxDescriptorSetSamplers;
    U32 maxDescriptorSetUniformBuffers, maxDescriptorSetUniformBuffersDynamic, maxDescriptorSetStorageBuffers;
    U32 maxDescriptorSetStorageBuffersDynamic, maxDescriptorSetSampledImages, maxDescriptorSetStorageImages;
    U32 maxDescriptorSetInputAttachments, maxVertexInputAttributes, maxVertexInputBindings;
    U32 maxVertexInputAttributeOffset, maxVertexInputBindingStride, maxVertexOutputComponents;
    U32 maxTessellationGenerationLevel, maxTessellationPatchSize, maxTessellationControlPerVertexInputComponents;
    U32 maxTessellationControlPerVertexOutputComponents, maxTessellationControlPerPatchOutputComponents;
    U32 maxTessellationControlTotalOutputComponents, maxTessellationEvaluationInputComponents;
    U32 maxTessellationEvaluationOutputComponents, maxGeometryShaderInvocations, maxGeometryInputComponents;
    U32 maxGeometryOutputComponents, maxGeometryOutputVertices, maxGeometryTotalOutputComponents;
    U32 maxFragmentInputComponents, maxFragmentOutputAttachments, maxFragmentDualSrcAttachments;
    U32 maxFragmentCombinedOutputResources, maxComputeSharedMemorySize, maxComputeWorkGroupCount[3];
    U32 maxComputeWorkGroupInvocations, maxComputeWorkGroupSize[3], subPixelPrecisionBits;
    U32 subTexelPrecisionBits, mipmapPrecisionBits, maxDrawIndexedIndexValue, maxDrawIndirectCount;
    float maxSamplerLodBias, maxSamplerAnisotropy;
    U32 maxViewports, maxViewportDimensions[2];
    float viewportBoundsRange[2];
    U32 viewportSubPixelBits;
    std::size_t minMemoryMapAlignment;
    U64 minTexelBufferOffsetAlignment, minUniformBufferOffsetAlignment, minStorageBufferOffsetAlignment;
    std::int32_t minTexelOffset;
    U32 maxTexelOffset;
    std::int32_t minTexelGatherOffset;
    U32 maxTexelGatherOffset;
    float minInterpolationOffset, maxInterpolationOffset;
    U32 subPixelInterpolationOffsetBits, maxFramebufferWidth, maxFramebufferHeight, maxFramebufferLayers;
    U32 framebufferColorSampleCounts, framebufferDepthSampleCounts, framebufferStencilSampleCounts;
    U32 framebufferNoAttachmentsSampleCounts, maxColorAttachments, sampledImageColorSampleCounts;
    U32 sampledImageIntegerSampleCounts, sampledImageDepthSampleCounts, sampledImageStencilSampleCounts;
    U32 storageImageSampleCounts, maxSampleMaskWords, timestampComputeAndGraphics;
    float timestampPeriod;
    U32 maxClipDistances, maxCullDistances, maxCombinedClipAndCullDistances, discreteQueuePriorities;
    float pointSizeRange[2], lineWidthRange[2], pointSizeGranularity, lineWidthGranularity;
    U32 strictLines, standardSampleLocations;
    U64 optimalBufferCopyOffsetAlignment, optimalBufferCopyRowPitchAlignment, nonCoherentAtomSize;
};
struct Properties {
    U32 apiVersion, driverVersion, vendorID, deviceID, deviceType;
    char deviceName[256];
    std::uint8_t pipelineCacheUUID[16];
    Limits limits;
    U32 sparseProperties[5];
};
struct Properties2 { U32 sType; void* pNext; Properties properties; };
struct Identity {
    U32 sType;
    void* pNext;
    std::uint8_t deviceUUID[16], driverUUID[16], deviceLUID[8];
    U32 deviceNodeMask, deviceLUIDValid;
};
struct Application {
    U32 sType;
    const void* pNext;
    const char* pApplicationName;
    U32 applicationVersion;
    const char* pEngineName;
    U32 engineVersion, apiVersion;
};
struct InstanceInfo {
    U32 sType;
    const void* pNext;
    U32 flags;
    const Application* pApplicationInfo;
    U32 enabledLayerCount;
    const char* const* ppEnabledLayerNames;
    U32 enabledExtensionCount;
    const char* const* ppEnabledExtensionNames;
};
struct Memory { U64 total, free, used; };
static_assert(sizeof(Limits) == 504 && offsetof(Limits, minMemoryMapAlignment) == 304);
static_assert(sizeof(Properties) == 824 && offsetof(Properties, limits) == 296);
static_assert(sizeof(Properties2) == 840 && sizeof(Identity) == 64);
static_assert(sizeof(Application) == 48 && sizeof(InstanceInfo) == 64 && sizeof(Memory) == 24);

struct Library {
    HMODULE module = nullptr;
    explicit Library(const wchar_t* name) : module(LoadLibraryExW(name, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32)) {}
    ~Library() { if (module) FreeLibrary(module); }
    Library(const Library&) = delete;
    Library& operator=(const Library&) = delete;
    template<class Function> Function Get(const char* name) const {
        return module ? std::bit_cast<Function>(GetProcAddress(module, name)) : nullptr;
    }
};

bool SoleVulkanGpuUuid(std::array<std::uint8_t, 16>& uuid) {
    Library vulkan(L"vulkan-1.dll");
    using Proc = void (WINAPI*)();
    using Address = Proc (WINAPI*)(void*, const char*);
    using Create = int (WINAPI*)(const InstanceInfo*, const void*, void**);
    using Destroy = void (WINAPI*)(void*, const void*);
    using Enumerate = int (WINAPI*)(void*, U32*, void**);
    using GetProperties = void (WINAPI*)(void*, Properties2*);
    const auto address = vulkan.Get<Address>("vkGetInstanceProcAddr");
    const auto create = vulkan.Get<Create>("vkCreateInstance");
    const auto destroy = vulkan.Get<Destroy>("vkDestroyInstance");
    if (!address || !create || !destroy) return false;
    const Application application{0, nullptr, "Einzig resource probe", 0, nullptr, 0, (1U << 22) | (1U << 12)};
    const InstanceInfo info{1, nullptr, 0, &application, 0, nullptr, 0, nullptr};
    void* instance = nullptr;
    if (create(&info, nullptr, &instance) != 0 || !instance) return false;
    struct InstanceLease {
        void* instance;
        Destroy destroy;
        ~InstanceLease() { destroy(instance, nullptr); }
    } lease{instance, destroy};
    const auto enumerate = std::bit_cast<Enumerate>(address(instance, "vkEnumeratePhysicalDevices"));
    const auto properties = std::bit_cast<GetProperties>(address(instance, "vkGetPhysicalDeviceProperties2"));
    if (!enumerate || !properties) return false;
    U32 count = 0;
    if (enumerate(instance, &count, nullptr) != 0 || count != 1) return false;
    void* physical = nullptr;
    if (enumerate(instance, &count, &physical) != 0 || count != 1 || !physical) return false;
    Identity identity{};
    identity.sType = 1000071004; // VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES
    Properties2 details{};
    details.sType = 1000059001; // VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2
    details.pNext = &identity;
    properties(physical, &details);
    if (details.properties.vendorID != 0x10de || details.properties.deviceType != 2 ||
        details.properties.apiVersion < application.apiVersion ||
        std::all_of(std::begin(identity.deviceUUID), std::end(identity.deviceUUID), [](auto byte) { return byte == 0; }))
        return false;
    // Exactly one physical device avoids assuming a DXGI/NVML ordinal equals
    // ncnn GPU0; multi-adapter/filter/order ambiguity always stays conservative.
    std::copy(std::begin(identity.deviceUUID), std::end(identity.deviceUUID), uuid.begin());
    return true;
}

void ProbeGpu(ResourceSnapshot& sample, int gpu) {
    if (gpu != 0) return;
    std::array<std::uint8_t, 16> identity{};
    if (!SoleVulkanGpuUuid(identity)) return;
    Library nvml(L"nvml.dll");
    using Initialize = int (*)();
    using Shutdown = int (*)();
    using GetHandle = int (*)(const char*, void**);
    using GetUuid = int (*)(void*, char*, unsigned);
    using GetMemory = int (*)(void*, Memory*);
    const auto initialize = nvml.Get<Initialize>("nvmlInit_v2");
    const auto shutdown = nvml.Get<Shutdown>("nvmlShutdown");
    const auto getHandle = nvml.Get<GetHandle>("nvmlDeviceGetHandleByUUID");
    const auto getUuid = nvml.Get<GetUuid>("nvmlDeviceGetUUID");
    const auto getMemory = nvml.Get<GetMemory>("nvmlDeviceGetMemoryInfo");
    if (!initialize || !shutdown || !getHandle || !getUuid || !getMemory || initialize() != 0) return;
    struct NvmlLease { Shutdown shutdown; ~NvmlLease() { shutdown(); } } lease{shutdown};
    std::array<char, 80> uuid{};
    std::snprintf(uuid.data(), uuid.size(),
        "GPU-%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        identity[0], identity[1], identity[2], identity[3], identity[4], identity[5], identity[6], identity[7],
        identity[8], identity[9], identity[10], identity[11], identity[12], identity[13], identity[14], identity[15]);
    void* device = nullptr;
    std::array<char, 96> verified{};
    Memory memory{};
    if (getHandle(uuid.data(), &device) != 0 || !device ||
        getUuid(device, verified.data(), static_cast<unsigned>(verified.size())) != 0 ||
        verified.back() != '\0' || _stricmp(uuid.data(), verified.data()) != 0 ||
        getMemory(device, &memory) != 0 || memory.total == 0 || memory.free > memory.total || memory.used > memory.total)
        return;
    sample.gpuTotal = memory.total;
    sample.gpuAvailable = memory.free;
    sample.gpuKnown = true;
}
} // namespace driver
#endif
} // namespace

ResourceSnapshot ProbeResources(const std::filesystem::path& directory, int gpu) {
    ResourceSnapshot sample;
    MEMORYSTATUSEX memory{};
    memory.dwLength = sizeof(memory);
    if (GlobalMemoryStatusEx(&memory)) {
        sample.totalPhysical = memory.ullTotalPhys;
        sample.availablePhysical = memory.ullAvailPhys;
        // This process's available commit is <= system-wide available commit.
        // It is a conservative limit, never the total pagefile size.
        sample.availableCommit = memory.ullAvailPageFile;
        sample.memoryKnown = sample.totalPhysical != 0 && sample.availablePhysical <= sample.totalPhysical;
    }
    ProbeDisk(directory, sample);
    // Optional driver telemetry never creates a logical device or runs a model.
    // Unknown is safe: ComputeLimits then permits only a <=256MiB single page.
#if defined(_WIN64)
    driver::ProbeGpu(sample, gpu);
#else
    (void)gpu;
#endif
    return sample;
}

ResourceLimits ComputeLimits(const ResourceSnapshot& sample, std::uint64_t batchOutputBytes, const UserLimits& user) {
    ResourceLimits limits;
    limits.gpuKnown = sample.gpuKnown;
    if (!ValidateUserLimits(user, limits.reason)) return limits;
    limits.cpuPercent = user.manual ? user.cpuPercent : 100;
    if (!ValidMemory(sample)) {
        limits.reason = L"無法確認可用記憶體。 / Available memory could not be verified.";
        return limits;
    }
    limits.totalBytes = user.manual ? std::min({user.ramBytes,
        Subtract(sample.totalPhysical, PhysicalReserve(sample)), Subtract(sample.availablePhysical, PhysicalReserve(sample)),
        Subtract(sample.availableCommit, 2 * GiB)}) :
        std::min({sample.totalPhysical / 8, sample.availablePhysical / 4,
            Subtract(sample.availablePhysical, PhysicalReserve(sample)), sample.availableCommit / 4});
    limits.cacheBytes = limits.displayBytes = limits.workBytes = limits.transientBytes = limits.totalBytes / 8;
    limits.workBytes = std::min(GiB, limits.workBytes);
    limits.jobBytes = limits.totalBytes / 2;
    limits.outputBytes = std::min(GiB, limits.workBytes);
    if (limits.totalBytes < GiB || sample.availableCommit < 2 * GiB) {
        limits.reason = L"可用 RAM 或 commit 不足，已暫停 AI。 / AI paused: insufficient RAM or commit headroom.";
        return limits;
    }
    if (!ValidDisk(sample)) {
        limits.reason = L"無法確認暫存磁碟空間。 / Temporary-volume space could not be verified.";
        return limits;
    }
    if (!ValidGpu(sample)) {
        limits.reason = L"無法確認顯卡資源數值。 / GPU resource telemetry is inconsistent.";
        return limits;
    }
    if (!sample.gpuKnown && user.manual && user.gpuReserveBytes) {
        limits.reason = L"無法確認手動顯存保留門檻，已暫停 AI。 / AI paused: the requested GPU reserve cannot be verified.";
        return limits;
    }
    limits.batchPages = std::min(sample.gpuKnown ? 8U : 1U, user.manual ? user.batchPages : 8U);
    if (sample.gpuKnown) {
        // Additional to the protected global reserve, keep 512MiB for model/tile
        // state and budget four output copies for display, input and work peaks.
        // This is an estimate; the watchdog and runtime hard limits still apply.
        const auto gpuWork = Subtract(Subtract(sample.gpuAvailable, GpuReserve(sample, user)), 512 * MiB) / 4;
        limits.workBytes = std::min(limits.workBytes, gpuWork);
    } else {
        limits.workBytes = std::min(limits.workBytes, 256 * MiB);
    }
    if (sample.displayBudget != 0) {
        // Parent-only DXGI budget may reduce display allocations. It never
        // establishes gpuKnown, child budget, or global available VRAM.
        limits.displayBytes = std::min(limits.displayBytes, Subtract(sample.displayBudget, sample.displayUsage));
    }
    limits.workBytes = std::min(limits.workBytes, limits.displayBytes);
    limits.outputBytes = std::min(limits.outputBytes, limits.workBytes);
    const auto stagingOutput = batchOutputBytes != 0 ? batchOutputBytes : limits.workBytes;
    limits.stagingBytes = stagingOutput >= GiB ? 4 * GiB : stagingOutput * 4;
    // With no proposed batch, report the available cache and prospective staging
    // cap. A concrete batch must call again with its output estimate before work.
    const auto reservedStaging = batchOutputBytes != 0 ? limits.stagingBytes : 0;
    limits.diskBytes = std::min(user.manual ? user.diskBytes : sample.diskAvailable / 10,
        Subtract(Subtract(sample.diskAvailable, DiskReserve(sample)), reservedStaging));
    if (limits.workBytes == 0) {
        limits.reason = L"可用顯存或顯示額度不足，已暫停 AI。 / AI paused: insufficient GPU or display headroom.";
        return limits;
    }
    if (batchOutputBytes > limits.workBytes) {
        limits.reason = L"批次輸出超過資源額度，請改用較低倍率。 / Batch output exceeds the resource budget; use a lower scale.";
        return limits;
    }
    if (limits.diskBytes == 0 || sample.diskAvailable <= DiskReserve(sample)) {
        limits.reason = L"暫存磁碟保留空間不足，已暫停 AI。 / AI paused: insufficient temporary-volume reserve.";
        return limits;
    }
    limits.allowed = true;
    return limits;
}

bool ResourcePressure(const ResourceSnapshot& sample, std::wstring& reason, const UserLimits& user) {
    const auto limits = ComputeLimits(sample, 0, user);
    reason = limits.reason;
    return !limits.allowed;
}

bool ResourcesRecovered(const ResourceSnapshot& sample, const UserLimits& user) {
    if (!ComputeLimits(sample, 0, user).allowed) return false;
    const auto margin = std::max(512 * MiB, Percent(sample.totalPhysical, 2));
    if (sample.availablePhysical <= Add(PhysicalReserve(sample), margin) ||
        sample.availableCommit <= Add(2 * GiB, margin) ||
        sample.diskAvailable <= Add(DiskReserve(sample), margin)) return false;
    if (sample.gpuKnown) {
        const auto gpuMargin = std::max(128 * MiB, Percent(sample.gpuTotal, 2));
        if (sample.gpuAvailable <= Add(Add(GpuReserve(sample, user), 512 * MiB), gpuMargin)) return false;
    }
    return true;
}
} // namespace ai
