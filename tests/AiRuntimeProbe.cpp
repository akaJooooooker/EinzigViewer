#include "AiRuntime.h"

#include <Windows.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
using namespace std::chrono_literals;

namespace {
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void PrintError(const std::wstring& error) {
    const auto bytes = WideCharToMultiByte(CP_UTF8, 0, error.data(), static_cast<int>(error.size()),
        nullptr, 0, nullptr, nullptr);
    std::string utf8(bytes, '\0');
    WideCharToMultiByte(CP_UTF8, 0, error.data(), static_cast<int>(error.size()), utf8.data(), bytes, nullptr, nullptr);
    std::cerr << utf8 << '\n';
}
void Write(const fs::path& path, const std::string& value) {
    std::ofstream(path, std::ios::binary) << value;
}
std::string Read(const fs::path& path) {
    std::ifstream file(path);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
std::size_t Count(const fs::path& path) {
    return fs::exists(path) ? static_cast<std::size_t>(std::distance(
        fs::directory_iterator(path), fs::directory_iterator{})) : 0;
}
bool WaitForFile(const fs::path& file, std::chrono::milliseconds timeout = 5s) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!fs::exists(file) && std::chrono::steady_clock::now() < deadline) Sleep(10);
    return fs::exists(file);
}
bool JobLimitsMatch(const fs::path& file, unsigned cpuPercent, std::uint64_t memoryBytes) {
    DWORD cpuFlags = 0, cpuRate = 0, memoryFlags = 0, priority = 0;
    std::uint64_t jobMemory = 0, processMemory = 0;
    std::ifstream record(file);
    if (!(record >> cpuFlags >> cpuRate >> memoryFlags >> jobMemory >> processMemory >> priority)) return false;
    const DWORD requiredMemoryFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE |
        JOB_OBJECT_LIMIT_JOB_MEMORY | JOB_OBJECT_LIMIT_PROCESS_MEMORY;
    const DWORD breakawayFlags = JOB_OBJECT_LIMIT_BREAKAWAY_OK | JOB_OBJECT_LIMIT_SILENT_BREAKAWAY_OK;
    const DWORD expectedCpuFlags = cpuPercent == 100 ? 0 :
        JOB_OBJECT_CPU_RATE_CONTROL_ENABLE | JOB_OBJECT_CPU_RATE_CONTROL_HARD_CAP;
    return cpuFlags == expectedCpuFlags && cpuRate == (cpuPercent == 100 ? 0 : cpuPercent * 100) &&
        (memoryFlags & requiredMemoryFlags) == requiredMemoryFlags && !(memoryFlags & breakawayFlags) &&
        jobMemory == memoryBytes && processMemory == memoryBytes && priority == BELOW_NORMAL_PRIORITY_CLASS;
}
ImageData Source() {
    ImageData image;
    image.path = L"original source 中文.png";
    image.sourceFingerprint = "fixture-original-content";
    image.width = 3;
    image.height = 2;
    image.stride = 12;
    image.pixels = {
        std::byte{10}, std::byte{20}, std::byte{30}, std::byte{255},
        std::byte{40}, std::byte{50}, std::byte{60}, std::byte{255},
        std::byte{70}, std::byte{80}, std::byte{90}, std::byte{255},
        std::byte{100}, std::byte{110}, std::byte{120}, std::byte{255},
        std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0},
        std::byte{20}, std::byte{40}, std::byte{60}, std::byte{128}};
    return image;
}
}

int wmain(int argc, wchar_t** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;
    if (argc != 2) { std::cerr << "Usage: AiRuntimeProbe <AiFakeProcess.exe>\n"; return 2; }
    const auto root = fs::absolute(fs::path(argv[1]).parent_path() /
        (L"AI 中文 runtime probe " + std::to_wstring(GetCurrentProcessId())));
    try {
        fs::create_directories(root);
        struct Cleanup { fs::path path; ~Cleanup() { std::error_code error; fs::remove_all(path, error); } } cleanup{root};
        const auto models = root / L"models-se";
        fs::create_directories(models);
        const auto child = root / L"AI 推論 process.exe";
        fs::copy_file(argv[1], child);
        for (const auto scale : {2, 4}) {
            const auto stem = L"up" + std::to_wstring(scale) + L"x-no-denoise";
            Write(models / (stem + L".param"), "fixture");
            Write(models / (stem + L".bin"), "fixture");
        }
        const auto work = root / L"work 暫存";
        const auto untouched = root / L"unrelated.txt";
        Write(untouched, "keep");
        ai::RuntimeOptions options;
        options.executable = child;
        options.modelDirectory = fs::path(models.wstring() + L"\\");
        options.timeoutMs = 15000;
        std::atomic_bool cancelled{false};
        std::wstring error;
        const auto source = Source();
        const auto firstStarted = std::chrono::steady_clock::now();

        // A missing CPU cap, silently ignored invalid request, or the old 4GiB
        // input ceiling must fail observable child/descendant Job state checks.
        unsigned resourceFailures = 0;
        const auto resourceCheck = [&](bool passed, const std::string& name) {
            std::cout << (passed ? "PASS " : "FAIL ") << name << '\n';
            if (!passed) { ++resourceFailures; PrintError(error); }
        };
        resourceCheck(options.cpuPercent == 100, "default CPU percentage is 100");
        Write(models / L"mode.txt", "job-query");
        for (const unsigned cpu : {100U, 37U, 1U, 99U}) {
            auto capped = options;
            capped.cpuPercent = cpu;
            fs::remove(models / L"child-limits.txt");
            fs::remove(models / L"descendant-limits.txt");
            const auto output = ai::Run(capped, source, 2, work, cancelled, error);
            resourceCheck(output.image && error.empty() &&
                JobLimitsMatch(models / L"child-limits.txt", cpu, capped.jobMemoryBytes),
                "CPU " + std::to_string(cpu) + "% child Job limits at entry");
            resourceCheck(JobLimitsMatch(models / L"descendant-limits.txt", cpu, capped.jobMemoryBytes),
                "CPU " + std::to_string(cpu) + "% descendant inherits Job limits");
        }
        auto roomy = options;
        roomy.cpuPercent = 37;
        roomy.jobMemoryBytes = 8ULL * 1024 * 1024 * 1024;
        fs::remove(models / L"child-limits.txt");
        fs::remove(models / L"descendant-limits.txt");
        {
            const auto output = ai::Run(roomy, source, 2, work, cancelled, error);
            resourceCheck(output.image && error.empty() &&
                JobLimitsMatch(models / L"child-limits.txt", 37, roomy.jobMemoryBytes) &&
                JobLimitsMatch(models / L"descendant-limits.txt", 37, roomy.jobMemoryBytes),
                "8GiB child and Job memory limit accepted with CPU 37%");
        }
        Write(models / L"mode.txt", "valid");
        for (const unsigned cpu : {0U, 101U, std::numeric_limits<unsigned>::max()}) {
            auto invalid = options;
            invalid.cpuPercent = cpu;
            fs::remove_all(work);
            fs::remove(models / L"launched.txt");
            const auto output = ai::Run(invalid, source, 2, work, cancelled, error);
            resourceCheck(!output.image && !error.empty() && !fs::exists(work) &&
                !fs::exists(models / L"launched.txt"), "invalid CPU " + std::to_string(cpu) + "% fails before launch");
        }
        {
            auto invalid = options;
            invalid.jobMemoryBytes = 0;
            fs::remove_all(work);
            fs::remove(models / L"launched.txt");
            resourceCheck(!ai::Run(invalid, source, 2, work, cancelled, error).image &&
                !error.empty() && !fs::exists(work) && !fs::exists(models / L"launched.txt"),
                "zero Job memory still fails before launch");
        }
        Require(resourceFailures == 0, "CPU/memory containment contracts failed");
        fs::remove(models / L"launches.txt");

        // A missing batch implementation or a per-page process fails these observable contracts.
        Require(ai::ValidateOutputSize(source, 8, error), "8x dimensions rejected");
        std::vector<ai::BatchInput> batch{{L"first", std::make_shared<ImageData>(source)},
            {L"second", std::make_shared<ImageData>(source)}};
        std::vector<ai::Output> completed;
        ai::RunBatch(options, batch, 2, work, cancelled,
            [&](std::size_t index, ai::Output output) {
                Require(index == completed.size(), "batch completed out of order");
                completed.push_back(std::move(output));
            }, error);
        if (completed.size() != 2 || !error.empty()) {
            PrintError(error);
            std::cerr << "completed=" << completed.size() << ", launches=" << Read(models / L"launches.txt") << '\n';
            std::cerr << "elapsedMs=" << std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - firstStarted).count() << '\n';
        }
        Require(completed.size() == 2 && error.empty(), "directory batch did not complete");
        Require(completed[0].artifact.png.parent_path() != completed[1].artifact.png.parent_path(),
            "completed pages share a batch cleanup lease");
        Require(Read(models / L"launches.txt") == "2\n", "batch launched a process per page");
        Require(completed[0].image->sourceFingerprint == source.sourceFingerprint,
            "AI result lost original content identity");
        completed.clear();
        std::cout << "PASS directory batch independent publications (" <<
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - firstStarted).count() << " ms)\n";

        fs::remove(models / L"launches.txt");
        fs::remove(models / L"finished.txt");
        Write(models / L"mode.txt", "progressive");
        bool early = false;
        ai::RunBatch(options, batch, 2, work, cancelled,
            [&](std::size_t index, ai::Output output) {
                Require(fs::exists(models / L"writer-closed.txt"), "open writer PNG published");
                if (index == 0) early = !fs::exists(models / L"finished.txt");
                completed.push_back(std::move(output));
                // Child waits after first result; cancellation here catches publishing only at exit.
                if (index == 0) cancelled.store(true);
            }, error);
        Require(early && completed.size() == 1 && !error.empty(), "progressive callback waited for child exit");
        const auto retained = completed.front().artifact.png;
        Require(fs::exists(retained), "cancellation destroyed verified page");
        Require(Count(work) == 1, "cancelled batch leaked intermediate directory");
        completed.clear(); cancelled.store(false);
        Require(Count(work) == 0, "independent result lease failed cleanup");
        std::cout << "PASS closed-file progressive publish survives cancellation\n";

        for (const auto mode : {"partial", "partial-corrupt"}) {
            Write(models / L"mode.txt", mode);
            ai::RunBatch(options, batch, 2, work, cancelled,
                [&](std::size_t index, ai::Output output) {
                    Require(index == 0, "invalid partial output published");
                    completed.push_back(std::move(output));
                }, error);
            Require(completed.size() == 1 && !error.empty() && fs::exists(completed[0].artifact.png),
                "partial failure discarded valid result");
            completed.clear();
        }
        std::cout << "PASS partial nonzero/corrupt output preserves verified pages\n";

        Write(models / L"mode.txt", "valid");
        fs::remove(models / L"launches.txt");
        auto eight = ai::Run(options, source, 8, work, cancelled, error);
        Require(eight.image && error.empty() && eight.image->width == 24 && eight.image->height == 16,
            "8x cascade dimensions incorrect");
        Require(Read(models / L"launches.txt") == "4\n2\n", "8x did not run exactly 4x then 2x");
        Require(eight.image->pixels[0] == std::byte{10}, "8x cascade changed opaque fixture");
        eight = {};
        Write(models / L"mode.txt", "second-fails");
        Require(!ai::Run(options, source, 8, work, cancelled, error).image && !error.empty(),
            "failed 2x stage published intermediate 4x image");
        Write(models / L"mode.txt", "stage-delay");
        options.timeoutMs = 150;
        auto batchStarted = std::chrono::steady_clock::now();
        Require(!ai::Run(options, source, 8, work, cancelled, error).image && !error.empty(),
            "8x restarted timeout for second stage");
        Require(std::chrono::steady_clock::now() - batchStarted < 1s, "8x shared timeout cleanup blocked");
        options.timeoutMs = 15000;
        std::cout << "PASS 8x exact cascade, second-stage failure and shared deadline\n";

        Write(models / L"mode.txt", "memory");
        auto restricted = options;
        restricted.jobMemoryBytes = 64ULL * 1024 * 1024;
        Require(!ai::Run(restricted, source, 2, work, cancelled, error).image && !error.empty(),
            "overcommitting child returned valid image");
        Require(Read(models / L"memory.txt") == "denied", "Job memory cap did not deny real 128MiB commit");
        Write(models / L"mode.txt", "hang");
        restricted = options;
        const auto pressureStarted = GetTickCount64();
        restricted.stopReason = [pressureStarted] {
            return GetTickCount64() - pressureStarted > 150 ? L"controlled resource pressure" : std::wstring{};
        };
        Require(!ai::Run(restricted, source, 2, work, cancelled, error).image &&
            error.find(L"controlled resource pressure") != std::wstring::npos, "resource stop reason ignored");
        Require(Count(work) == 0, "resource/memory failure leaked work");
        std::cout << "PASS hard child memory cap and injected resource stop\n";

        fs::remove_all(work); fs::remove(models / L"launched.txt");
        for (const int limit : {0, 1, 2}) {
            restricted = options;
            if (limit == 0) restricted.maximumOutputBytes = 95;
            if (limit == 1) restricted.maximumBatchBytes = 191;
            if (limit == 2) restricted.stagingBytes = 1;
            ai::RunBatch(restricted, batch, 2, work, cancelled,
                [&](std::size_t, ai::Output) { throw std::runtime_error("preflight published output"); }, error);
            Require(!error.empty() && !fs::exists(work) && !fs::exists(models / L"launched.txt"),
                "batch resource preflight created files or child");
        }
        ImageData large;
        large.width = 1000; large.height = 1500;
        Require(!ai::ValidateOutputSize(large, 8, error) &&
            ai::ValidateOutputSize(large, 8, error, 1024ULL * 1024 * 1024),
            "explicit output ceiling did not unlock bounded 8x");
        Require(!ai::ValidateOutputSize(source, 2, error, 1024, 5), "device dimension cap ignored");
        Write(models / L"mode.txt", "valid");
        std::cout << "PASS output/sum/staging/device preflight before file creation\n";

        Write(models / L"mode.txt", "serialization");
        ai::Output parallelA, parallelB;
        std::wstring parallelErrorA, parallelErrorB;
        std::thread concurrentA([&] { parallelA = ai::Run(options, source, 2, work, cancelled, parallelErrorA); });
        std::thread concurrentB([&] { parallelB = ai::Run(options, source, 2, work, cancelled, parallelErrorB); });
        concurrentA.join(); concurrentB.join();
        Require(parallelA.image && parallelB.image && parallelErrorA.empty() && parallelErrorB.empty(),
            "concurrent callers launched overlapping AI children");
        parallelA = {}; parallelB = {};
        Write(models / L"mode.txt", "valid");
        std::cout << "PASS concurrent callers share one AI child slot\n";
        fs::remove_all(work);
        fs::remove(models / L"launched.txt");

        // Removing the output-size guard must permit an oversized launch and fail here.
        Require(ai::ValidateOutputSize(source, 2, error), "valid dimensions rejected");
        for (const auto scale : {0, 1, 3, -1, std::numeric_limits<int>::max()})
            Require(!ai::ValidateOutputSize(source, scale, error) && !error.empty(), "invalid scale accepted");
        for (const auto dimensions : std::vector<std::pair<unsigned, unsigned>>{
                 {0, 2}, {2, 0}, {8193, 1}, {4097, 4096}, {0xffffffffU, 0xffffffffU}}) {
            ImageData invalid;
            invalid.width = dimensions.first;
            invalid.height = dimensions.second;
            Require(!ai::ValidateOutputSize(invalid, 2, error) && !error.empty(), "oversized dimensions accepted");
            Require(!ai::Run(options, invalid, 2, work, cancelled, error).image, "oversized input launched");
            Require(!fs::exists(work) && !fs::exists(models / L"launched.txt"), "oversized input created work");
        }
        std::cout << "PASS invalid/overflow sizes before launch\n";

        // Wrong Windows quoting, source mutation or premature lease cleanup breaks this round-trip.
        for (const auto scale : {2, 4}) {
            auto output = ai::Run(options, source, scale, work, cancelled, error);
            if (!output.image) std::wcerr << error << L'\n';
            Require(output.image != nullptr && error.empty(), "valid process did not return image");
            Require(output.image->width == (scale == 2 ? 6U : 12U) &&
                output.image->height == (scale == 2 ? 4U : 8U), "output dimensions differ");
            Require(output.image->path == source.path, "output lost source path");
            Require(output.image->pixels[0] == std::byte{10} &&
                output.image->pixels[1] == std::byte{20} &&
                output.image->pixels[2] == std::byte{30}, "PNG changed opaque source pixels");
            const auto transparentOffset = static_cast<std::size_t>(scale) * output.image->stride + scale * 4;
            Require(output.image->pixels[transparentOffset + 3] == std::byte{0}, "PNG lost alpha");
            Require(output.artifact.lease && fs::exists(output.artifact.png), "missing leased PNG");
            auto keep = output.artifact.lease;
            const auto directory = output.artifact.png.parent_path();
            output = {};
            Require(fs::exists(directory), "shared lease removed live artifact");
            keep.reset();
            Require(!fs::exists(directory), "last lease failed to clean directory");
        }
        Require(fs::exists(untouched) && Count(work) == 0, "cleanup escaped owned directories");
        std::cout << "PASS 2x/4x Unicode/space/trailing-slash quoting, pixels, alpha, shared lease\n";

        for (const auto mode : {"nonzero", "none", "corrupt", "wrong"}) {
            Write(models / L"mode.txt", mode);
            auto output = ai::Run(options, source, 2, work, cancelled, error);
            Require(!output.image && !output.artifact.lease && !error.empty(), "invalid process output was published");
            if (std::string(mode) == "nonzero")
                Require(error.find(L"controlled inference failure") != std::wstring::npos, "diagnostic output lost");
            Require(Count(work) == 0, "failed process leaked work directory");
        }
        std::cout << "PASS nonzero/missing/corrupt/wrong-sized output\n";

        Write(models / L"mode.txt", "flood");
        auto flood = ai::Run(options, source, 2, work, cancelled, error);
        Require(flood.image != nullptr, "stdout/stderr flood deadlocked child");
        flood = {};
        std::cout << "PASS 32 MiB stdout/stderr flood\n";

        Write(models / L"mode.txt", "hang");
        options.timeoutMs = 150;
        auto started = std::chrono::steady_clock::now();
        Require(!ai::Run(options, source, 2, work, cancelled, error).image && !error.empty(), "hung process did not time out");
        Require(std::chrono::steady_clock::now() - started < 3s && Count(work) == 0, "timeout cleanup blocked or leaked");
        std::cout << "PASS timeout terminates process before cleanup\n";

        // Without the Job Object, a descendant survives and keeps held.txt undeletable.
        options.timeoutMs = 10000;
        Write(models / L"mode.txt", "descendant");
        ai::Output cancellationResult;
        std::thread worker([&] { cancellationResult = ai::Run(options, source, 2, work, cancelled, error); });
        const bool descendantStarted = WaitForFile(models / L"descendant.pid");
        started = std::chrono::steady_clock::now();
        cancelled.store(true);
        worker.join();
        Require(descendantStarted, "controlled descendant did not start");
        Require(!cancellationResult.image && !error.empty(), "cancelled output published");
        Require(std::chrono::steady_clock::now() - started < 1500ms && Count(work) == 0, "cancellation blocked or leaked");
        DWORD descendantPid = 0;
        std::ifstream(models / L"descendant.pid") >> descendantPid;
        const auto process = OpenProcess(SYNCHRONIZE, FALSE, descendantPid);
        if (process) {
            const auto status = WaitForSingleObject(process, 0);
            CloseHandle(process);
            Require(status == WAIT_OBJECT_0, "Job Object left descendant running");
        }
        Require(fs::remove(models / L"held.txt"), "descendant handle still holds file");
        std::cout << "PASS cancellation kills owned descendants and releases handles\n";

        fs::remove_all(work);
        fs::remove(models / L"launched.txt");
        Require(!ai::Run(options, source, 2, work, cancelled, error).image && !fs::exists(work), "pre-cancelled run created files");
        cancelled.store(false);
        Write(models / L"mode.txt", "valid");
        auto invalidOptions = options;
        invalidOptions.executable = root / L"missing.exe";
        Require(!ai::Run(invalidOptions, source, 2, work, cancelled, error).image && !fs::exists(work), "missing executable created work");
        invalidOptions = options;
        invalidOptions.gpu = -1;
        Require(!ai::Run(invalidOptions, source, 2, work, cancelled, error).image && !fs::exists(work), "negative GPU accepted");
        fs::remove(models / L"up2x-no-denoise.bin");
        Require(!ai::Run(options, source, 2, work, cancelled, error).image && !fs::exists(work), "missing model created work");
        Require(!fs::exists(models / L"launched.txt"), "invalid assets launched child");
        std::cout << "PASS cancelled/missing assets/negative GPU create no work\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
