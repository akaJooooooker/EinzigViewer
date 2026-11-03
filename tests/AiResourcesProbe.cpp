#include "AiResources.h"

#include <Windows.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
constexpr std::uint64_t MiB = 1024ULL * 1024;
constexpr std::uint64_t GiB = 1024ULL * MiB;

void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

ai::ResourceSnapshot Healthy() {
    ai::ResourceSnapshot value;
    value.totalPhysical = 64 * GiB;
    value.availablePhysical = 40 * GiB;
    value.availableCommit = 48 * GiB;
    value.diskTotal = 1000 * GiB;
    value.diskAvailable = 200 * GiB;
    value.gpuTotal = 16 * GiB;
    value.gpuAvailable = 12 * GiB;
    value.memoryKnown = value.diskKnown = value.gpuKnown = true;
    return value;
}

void MemoryPolicy() {
    auto large = Healthy();
    large.totalPhysical = 256 * GiB; large.availablePhysical = 200 * GiB;
    large.availableCommit = 256 * GiB;
    const auto automatic = ai::ComputeLimits(large);
    Require(automatic.allowed && automatic.totalBytes == 32 * GiB && automatic.diskBytes == 20 * GiB,
        "automatic budget still has a fixed 8GiB ceiling");
    // Losing any term of the minimum would allow excess live allocations.
    const auto high = ai::ComputeLimits(Healthy(), GiB);
    Require(high.allowed && high.totalBytes == 8 * GiB, "high RAM/commit budget missing");
    Require(high.cacheBytes == GiB && high.displayBytes == GiB && high.workBytes == GiB &&
        high.transientBytes == GiB && high.jobBytes == 4 * GiB, "budget portions overbook or undercount");
    Require(high.cacheBytes + high.displayBytes + high.workBytes + high.transientBytes + high.jobBytes <=
        high.totalBytes, "independent pools overspend total RAM budget");
    Require(high.outputBytes == GiB && high.batchPages == 8, "known GPU did not unlock bounded batches");

    struct Row { std::uint64_t ram, available, commit, want; };
    for (const auto& row : {
        Row{16 * GiB, 12 * GiB, 32 * GiB, 2 * GiB},
        Row{64 * GiB, 16 * GiB, 48 * GiB, 4 * GiB},
        Row{64 * GiB, 40 * GiB, 8 * GiB, 2 * GiB},
        // R=10GiB means 15% is below the 2GiB floor: A-reserve limits B.
        Row{10 * GiB, 3 * GiB, 32 * GiB, 768 * MiB}}) {
        auto sample = Healthy();
        sample.totalPhysical = row.ram;
        sample.availablePhysical = row.available;
        sample.availableCommit = row.commit;
        const auto limits = ai::ComputeLimits(sample);
        Require(limits.totalBytes == row.want, "RAM formula omitted a limiting term");
        Require(limits.allowed == (row.want >= GiB), "sub-GiB RAM budget was permitted");
    }
    auto sample = Healthy();
    sample.availablePhysical = 1;
    Require(ai::ComputeLimits(sample).totalBytes == 0 && !ai::ComputeLimits(sample).allowed,
        "physical reserve subtraction wrapped");
    sample = Healthy();
    sample.availableCommit = GiB;
    Require(!ai::ComputeLimits(sample).allowed, "low commit permitted inference");
    sample = Healthy();
    sample.memoryKnown = false;
    Require(!ai::ComputeLimits(sample).allowed, "unknown RAM permitted inference");
    sample = Healthy();
    sample.availablePhysical = sample.totalPhysical + 1;
    Require(!ai::ComputeLimits(sample).allowed, "inconsistent RAM telemetry permitted inference");
    std::cout << "PASS RAM/commit/reserve and shared pool accounting\n";
}

void DiskPolicy() {
    // Staging must be reserved before cache space and multiplication must saturate.
    const auto high = ai::ComputeLimits(Healthy(), GiB);
    Require(high.diskBytes == 20 * GiB && high.stagingBytes == 4 * GiB, "disk proportion/staging limits missing");
    auto sample = Healthy();
    sample.diskTotal = 100 * GiB;
    sample.diskAvailable = 6 * GiB;
    const auto tight = ai::ComputeLimits(sample, 256 * MiB);
    Require(!tight.allowed && tight.diskBytes == 0 && tight.stagingBytes == GiB,
        "staging consumed protected disk reserve");
    const auto available = ai::ComputeLimits(sample, 128 * MiB);
    Require(available.allowed && available.diskBytes == 512 * MiB && available.stagingBytes == 512 * MiB,
        "disk budget failed caller-available/reserve/staging formula");
    sample.diskAvailable = 1;
    Require(!ai::ComputeLimits(sample).allowed && ai::ComputeLimits(sample).diskBytes == 0,
        "disk reserve subtraction wrapped");
    sample = Healthy();
    sample.diskKnown = false;
    Require(!ai::ComputeLimits(sample).allowed, "unknown temp disk permitted inference");
    sample = Healthy();
    sample.diskAvailable = sample.diskTotal + 1;
    Require(!ai::ComputeLimits(sample).allowed, "invalid disk telemetry permitted inference");
    sample = Healthy();
    const auto overflow = ai::ComputeLimits(sample, std::numeric_limits<std::uint64_t>::max());
    Require(!overflow.allowed && overflow.stagingBytes == 4 * GiB,
        "oversized batch or staging multiplication overflow accepted");
    std::cout << "PASS caller-available disk, staging and saturation\n";
}

void GpuPolicy() {
    // Parent DXGI allowance cannot unlock global/child GPU allocations.
    auto sample = Healthy();
    sample.gpuKnown = false;
    sample.gpuTotal = sample.gpuAvailable = std::numeric_limits<std::uint64_t>::max();
    sample.displayBudget = 20 * GiB;
    const auto unknown = ai::ComputeLimits(sample);
    Require(unknown.allowed && !unknown.gpuKnown && unknown.batchPages == 1 &&
        unknown.outputBytes <= 256 * MiB && unknown.workBytes <= 256 * MiB,
        "unknown GPU escaped single-page conservative cap");
    sample = Healthy();
    sample.gpuAvailable = 512 * MiB;
    Require(!ai::ComputeLimits(sample).allowed, "known low VRAM ignored global reserve");
    sample.gpuAvailable = 3 * GiB;
    const auto limited = ai::ComputeLimits(sample);
    Require(limited.allowed && limited.outputBytes < 256 * MiB && limited.workBytes <= limited.outputBytes,
        "known GPU peak accounting did not reduce working output");
    sample = Healthy();
    sample.gpuAvailable = sample.gpuTotal + 1;
    Require(!ai::ComputeLimits(sample).allowed, "inconsistent known GPU telemetry accepted");
    sample = Healthy();
    sample.displayBudget = GiB;
    sample.displayUsage = 768 * MiB;
    const auto display = ai::ComputeLimits(sample);
    Require(display.displayBytes <= 256 * MiB && display.outputBytes <= display.displayBytes,
        "parent display allowance failed to constrain display/output");
    sample.displayUsage = 2 * GiB;
    Require(!ai::ComputeLimits(sample).allowed, "exhausted parent display budget wrapped");
    std::cout << "PASS reliable GPU reserve/peak and unknown-GPU/DXGI isolation\n";
}

void PressureAndRecovery() {
    std::wstring reason = L"old reason";
    Require(!ai::ResourcePressure(Healthy(), reason) && reason.empty() && ai::ResourcesRecovered(Healthy()),
        "healthy snapshot remained pressured");
    auto sample = Healthy();
    sample.availableCommit = 2 * GiB - 1;
    Require(ai::ResourcePressure(sample, reason) && !reason.empty() && !ai::ResourcesRecovered(sample),
        "commit pressure not detected");
    sample = Healthy();
    sample.totalPhysical = 256 * GiB;
    sample.availablePhysical = 100 * GiB;
    sample.availableCommit = 4 * GiB;
    // This has B=1GiB and is above reserve: recovery still needs added commit margin.
    Require(!ai::ResourcePressure(sample, reason) && !ai::ResourcesRecovered(sample),
        "restart omitted reserve margin");
    sample.availableCommit = 8 * GiB;
    Require(ai::ResourcesRecovered(sample), "healthy reserve margin failed to recover");
    sample = Healthy();
    sample.diskAvailable = 20 * GiB - 1;
    Require(ai::ResourcePressure(sample, reason) && !ai::ResourcesRecovered(sample), "disk pressure ignored");
    sample.diskAvailable = 20 * GiB + 1;
    Require(!ai::ResourcesRecovered(sample), "disk recovery omitted reserve margin");
    sample = Healthy();
    sample.gpuAvailable = GiB;
    Require(ai::ResourcePressure(sample, reason) && !ai::ResourcesRecovered(sample), "GPU pressure ignored");
    sample.gpuKnown = false;
    Require(!ai::ResourcePressure(sample, reason) && ai::ResourcesRecovered(sample),
        "unknown GPU prevented conservative restart");
    sample.memoryKnown = false;
    Require(ai::ResourcePressure(sample, reason) && !ai::ResourcesRecovered(sample), "probe failure ignored");
    std::cout << "PASS pressure reasons and one-sample recovery hysteresis\n";
}

void ExtremeArithmetic() {
    // Untrusted samples must not wrap percentages, reserves, sums or batch estimates.
    auto sample = Healthy();
    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    sample.totalPhysical = sample.availablePhysical = sample.availableCommit = maximum;
    sample.diskTotal = sample.diskAvailable = sample.gpuTotal = sample.gpuAvailable = maximum;
    const auto limits = ai::ComputeLimits(sample, GiB);
    Require(limits.allowed && limits.totalBytes == 2305843009213693951ULL && limits.diskBytes == 1844674407370955161ULL &&
        limits.stagingBytes == 4 * GiB && limits.outputBytes <= GiB && ai::ResourcesRecovered(sample),
        "maximum integers overflowed a quota calculation");
    sample = {};
    Require(!ai::ComputeLimits(sample).allowed && !ai::ResourcesRecovered(sample), "zero snapshot accepted");
    sample.memoryKnown = sample.diskKnown = sample.gpuKnown = true;
    Require(!ai::ComputeLimits(sample).allowed, "known zero totals accepted");
    std::cout << "PASS uint64 maximum/zero arithmetic\n";
}

void ReadOnlyProbe() {
    const auto current = std::filesystem::current_path();
    const auto missing = current / L"ai-resources-probe-must-not-create" / L"nested";
    Require(!std::filesystem::exists(missing), "probe fixture unexpectedly exists");
    const auto observed = ai::ProbeResources(missing, 0);
    Require(observed.memoryKnown && observed.totalPhysical != 0 && observed.availablePhysical <= observed.totalPhysical,
        "real Windows physical/commit probe failed");
    Require(observed.diskKnown && observed.diskTotal != 0 && observed.diskAvailable <= observed.diskTotal,
        "real cache-volume probe failed for nonexistent owned path");
    Require(!std::filesystem::exists(missing), "resource probe created a directory");
    const auto invalidGpu = ai::ProbeResources(current, -1);
    Require(!invalidGpu.gpuKnown && invalidGpu.gpuAvailable == 0 && invalidGpu.gpuTotal == 0,
        "invalid GPU ID inferred global GPU memory");
    const auto ambiguousGpu = ai::ProbeResources(current, 1);
    Require(!ambiguousGpu.gpuKnown && ambiguousGpu.gpuAvailable == 0 && ambiguousGpu.gpuTotal == 0,
        "GPU ordinal was substituted for a verified Vulkan identity");
    const auto invalidDisk = ai::ProbeResources(L"?:\\not-a-volume", 0);
    Require(!invalidDisk.diskKnown, "invalid cache path silently probed another volume");
    std::cout << "READ_ONLY physical=" << observed.totalPhysical << " available=" << observed.availablePhysical
        << " commit=" << observed.availableCommit << " diskTotal=" << observed.diskTotal
        << " diskAvailable=" << observed.diskAvailable << " gpuKnown=" << observed.gpuKnown
        << " gpuTotal=" << observed.gpuTotal << " gpuAvailable=" << observed.gpuAvailable << '\n';
    std::cout << "PASS read-only real RAM/commit/cache volume and adapter confidence\n";
}

void ManualPolicy() {
    ai::UserLimits user; user.manual = true; user.ramBytes = 16 * GiB;
    user.diskBytes = 24 * GiB; user.cpuPercent = 37; user.batchPages = 3;
    const auto manual = ai::ComputeLimits(Healthy(), GiB, user);
    Require(manual.allowed && manual.totalBytes == 16 * GiB && manual.jobBytes == 8 * GiB &&
        manual.diskBytes == 24 * GiB && manual.cpuPercent == 37 && manual.batchPages == 3,
        "manual limits did not reach shared pools, child CPU, cache or batch");
    Require(manual.workBytes == GiB && manual.outputBytes == GiB && manual.cacheBytes == 2 * GiB,
        "large manual pool bypassed 1GiB work/output bound");
    auto sample = Healthy(); sample.availablePhysical = 10 * GiB;
    const auto tight = ai::ComputeLimits(sample, 0, user);
    Require(!tight.allowed && tight.totalBytes == 429496730ULL,
        "manual physical reserve did not shrink the pool");
    sample = Healthy(); sample.availableCommit = 5 * GiB;
    Require(ai::ComputeLimits(sample, 0, user).totalBytes == 3 * GiB,
        "manual pool consumed protected commit reserve");
    user.gpuReserveBytes = 8 * GiB; sample = Healthy(); sample.gpuAvailable = 8 * GiB;
    std::wstring reason;
    Require(!ai::ComputeLimits(sample, 0, user).allowed && ai::ResourcePressure(sample, reason, user) &&
        !reason.empty() && !ai::ResourcesRecovered(sample, user), "manual GPU threshold was ignored");
    sample.gpuKnown = false;
    Require(!ai::ComputeLimits(sample, 0, user).allowed && !ai::ResourcesRecovered(sample, user),
        "unknown GPU silently removed an explicit reserve");
    sample = Healthy(); sample.gpuAvailable = 8 * GiB + 512 * MiB + 128 * MiB;
    Require(ai::ComputeLimits(sample, 0, user).allowed && !ai::ResourcesRecovered(sample, user),
        "manual GPU recovery omitted reserve margin");
    Require(ai::ResourcesRecovered(Healthy(), user), "manual GPU threshold never recovered");
    user.gpuReserveBytes = 0; sample.gpuKnown = false;
    Require(ai::ComputeLimits(sample, 0, user).allowed && ai::ComputeLimits(sample, 0, user).batchPages == 1,
        "automatic GPU reserve did not preserve unknown-GPU mode");
    for (unsigned value : {0U, 101U}) {
        user.cpuPercent = value;
        Require(!ai::ComputeLimits(Healthy(), 0, user).allowed, "invalid manual CPU ran inference");
    }
    user = {}; user.error = L"corrupt saved mode";
    Require(!ai::ComputeLimits(Healthy(), 0, user).allowed, "corrupt saved limits ran inference");
    std::cout << "PASS manual shared budget, CPU/batch and GPU threshold safety\n";
}

void SettingsPersistence() {
    const auto directory = std::filesystem::temp_directory_path() /
        (L"EinzigAiSettingsProbe-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(directory);
    const auto path = directory / L"settings.ini";
    struct Cleanup { std::filesystem::path directory; ~Cleanup() { std::error_code ignored; std::filesystem::remove_all(directory, ignored); } } cleanup{directory};
    const auto write = [&](const std::string& text) { std::ofstream file(path, std::ios::binary | std::ios::trunc); file << text; };
    write("[viewer]\nposition=17\n");
    Require(!ai::ReadUserLimits(path).manual && ai::ReadUserLimits(path).error.empty(), "absent section was not automatic");
    ai::UserLimits user; user.manual = true; user.ramBytes = 16 * GiB; user.diskBytes = 24 * GiB;
    user.cpuPercent = 100; user.batchPages = 8; user.gpuReserveBytes = 768 * MiB;
    std::wstring error;
    Require(ai::WriteUserLimits(path, user, error), "manual INI could not be written");
    const auto restored = ai::ReadUserLimits(path);
    Require(restored.error.empty() && restored.manual && restored.ramBytes == 16 * GiB && restored.diskBytes == 24 * GiB &&
        restored.cpuPercent == 100 && restored.batchPages == 8 && restored.gpuReserveBytes == 768 * MiB,
        "manual INI roundtrip changed a limit");
    Require(GetPrivateProfileIntW(L"viewer", L"position", 0, path.c_str()) == 17, "AI settings replaced another INI section");
    const std::string valid = "[aiResources]\nmode=manual\nramGiB=16\ndiskGiB=24\ncpuPercent=37\nbatchPages=3\ngpuReserveMiB=768\n";
    for (const auto& pair : {std::pair{"mode=manual", "mode=broken"}, {"cpuPercent=37", "cpuPercent=37oops"},
        {"ramGiB=16", "ramGiB=-1"}, {"diskGiB=24", "diskGiB=18446744073709551616"},
        {"batchPages=3", "batchPages=9"}, {"gpuReserveMiB=768", "gpuReserveMiB=1"},
        {"cpuPercent=37", "cpuPercent=0"}, {"ramGiB=16", "ramGiB=17179869184"},
        {"diskGiB=24", "diskGiB=1.5"}, {"batchPages=3", "missing=3"}}) {
        auto text = valid; text.replace(text.find(pair.first), std::char_traits<char>::length(pair.first), pair.second); write(text);
        const auto corrupt = ai::ReadUserLimits(path);
        Require(!corrupt.error.empty() && !ai::ComputeLimits(Healthy(), 0, corrupt).allowed,
            "corrupt manual INI silently removed a limit");
    }
    write("[aiResources]\n");
    Require(!ai::ReadUserLimits(path).error.empty(), "present empty section silently became automatic");
    write("[aiResources]\nmode=automatic\nramGiB=corrupt\ncpuPercent=-1\n");
    Require(!ai::ReadUserLimits(path).manual && ai::ReadUserLimits(path).error.empty(), "automatic mode read unused manual fields");
    user.cpuPercent = 0;
    Require(!ai::WriteUserLimits(path, user, error) && !error.empty() && !ai::ReadUserLimits(path).manual,
        "invalid settings were persisted");
    user.cpuPercent = 19;
    const auto nested = directory / L"first-run" / L"installed" / L"settings.ini";
    Require(ai::WriteUserLimits(nested, user, error) && ai::ReadUserLimits(nested).cpuPercent == 19,
        "first-run settings could not create their parent directory");
    std::cout << "PASS strict INI roundtrip/corruption and unrelated-section preservation\n";
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string(argv[1]) == "--manual") { ManualPolicy(); return 0; }
        if (argc == 2 && std::string(argv[1]) == "--settings") { SettingsPersistence(); return 0; }
        if (argc == 2 && std::string(argv[1]) == "--timings") {
            for (int index = 0; index < 6; ++index) {
                const auto started = std::chrono::steady_clock::now();
                const auto sample = ai::ProbeResources(std::filesystem::current_path(), 0);
                const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - started).count();
                std::cout << "NORMAL_PROBE index=" << index << " microseconds=" << elapsed
                    << " gpuKnown=" << sample.gpuKnown << '\n';
            }
            const auto started = std::chrono::steady_clock::now();
            const auto sample = ai::ProbeResources(L"?:\\not-a-volume", -1);
            std::cout << "INVALID_PATH_PROBE microseconds=" <<
                std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - started).count()
                << " diskKnown=" << sample.diskKnown << '\n';
            return 0;
        }
        MemoryPolicy();
        DiskPolicy();
        GpuPolicy();
        PressureAndRecovery();
        ExtremeArithmetic();
        ManualPolicy();
        SettingsPersistence();
        ReadOnlyProbe();
        // Opt-in local verification: this machine has existing NVIDIA/Vulkan
        // drivers. Ordinary CI remains valid without either optional driver.
        if (argc == 2 && std::string(argv[1]) == "--require-gpu") {
            const auto sample = ai::ProbeResources(std::filesystem::current_path(), 0);
            Require(sample.gpuKnown && sample.gpuTotal != 0 && sample.gpuAvailable <= sample.gpuTotal,
                "local Vulkan GPU0 identity/global NVML memory not verified");
            Require(ai::ComputeLimits(sample).batchPages == 8,
                "verified local GPU did not enable bounded batching");
            std::cout << "PASS local matched Vulkan GPU0 global telemetry and batching\n";
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
