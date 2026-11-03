#include "AiRuntime.h"

#include "ImageExporter.h"
#include "WicDecoder.h"

#include <Windows.h>
#include <objbase.h>

#include <algorithm>
#include <array>
#include <fstream>
#include <limits>
#include <mutex>
#include <chrono>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {
constexpr std::uint64_t MaximumDecodeBytes = 256ULL * 1024 * 1024;
constexpr std::size_t MaximumDiagnosticBytes = 16 * 1024;

class Handle {
public:
    explicit Handle(HANDLE value = nullptr) noexcept : value_(value) {}
    ~Handle() { reset(); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& other) noexcept : value_(std::exchange(other.value_, nullptr)) {}
    Handle& operator=(Handle&& other) noexcept {
        if (this != &other) reset(std::exchange(other.value_, nullptr));
        return *this;
    }
    HANDLE get() const noexcept { return value_; }
    explicit operator bool() const noexcept { return value_ && value_ != INVALID_HANDLE_VALUE; }
    void reset(HANDLE value = nullptr) noexcept {
        if (*this) CloseHandle(value_);
        value_ = value;
    }
private:
    HANDLE value_;
};

std::wstring WindowsError(const wchar_t* action, DWORD code = GetLastError()) {
    wchar_t* buffer = nullptr;
    const auto length = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, code, 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    std::wstring message(action);
    message += L" (" + std::to_wstring(code) + L")";
    if (length && buffer) message += L": " + std::wstring(buffer, length);
    if (buffer) LocalFree(buffer);
    while (!message.empty() && (message.back() == L'\r' || message.back() == L'\n')) message.pop_back();
    return message;
}

// Quote according to CommandLineToArgvW/MSVC rules, including a trailing backslash.
std::wstring Quote(const std::wstring& value) {
    std::wstring result = L"\"";
    std::size_t slashes = 0;
    for (const auto character : value) {
        if (character == L'\\') { ++slashes; continue; }
        result.append(slashes * (character == L'\"' ? 2 : 1), L'\\');
        slashes = 0;
        if (character == L'\"') result += L'\\';
        result += character;
    }
    result.append(slashes * 2, L'\\');
    return result + L'\"';
}

struct TemporaryDirectory {
    explicit TemporaryDirectory(std::filesystem::path value) : path(std::move(value)) {}
    ~TemporaryDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
    std::filesystem::path path;
};

struct Process {
    Handle job;
    Handle process;
    Handle thread;
    // The directory lease is declared before Process in Run. All owned processes
    // have exited before Process destruction allows that lease to be destroyed.
    ~Process() { Stop(); }
    void Stop() noexcept {
        if (!process) return;
        // Job accounting can reach zero before a terminating descendant's
        // process object is signaled. Keep synchronization handles before
        // requesting termination, then wait for full process teardown too.
        std::vector<Handle> ownedProcesses;
        if (job) {
            try {
                std::vector<std::byte> storage(sizeof(JOBOBJECT_BASIC_PROCESS_ID_LIST) + 16 * sizeof(ULONG_PTR));
                for (;;) {
                    auto* ids = reinterpret_cast<JOBOBJECT_BASIC_PROCESS_ID_LIST*>(storage.data());
                    if (QueryInformationJobObject(job.get(), JobObjectBasicProcessIdList,
                        ids, static_cast<DWORD>(storage.size()), nullptr)) {
                        ownedProcesses.reserve(ids->NumberOfProcessIdsInList);
                        for (DWORD index = 0; index < ids->NumberOfProcessIdsInList; ++index) {
                            Handle owned(OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                                static_cast<DWORD>(ids->ProcessIdList[index])));
                            BOOL contained = FALSE;
                            if (owned && IsProcessInJob(owned.get(), job.get(), &contained) && contained)
                                ownedProcesses.push_back(std::move(owned));
                        }
                        break;
                    }
                    if (GetLastError() != ERROR_MORE_DATA || ids->NumberOfAssignedProcesses > 4096) break;
                    const auto required = sizeof(JOBOBJECT_BASIC_PROCESS_ID_LIST) +
                        static_cast<std::size_t>(ids->NumberOfAssignedProcesses) * sizeof(ULONG_PTR);
                    storage.resize(std::max(storage.size() * 2, required));
                }
            } catch (...) {
                // Kill-on-close and job accounting remain the fallback even
                // when collecting synchronization handles cannot allocate.
            }
        }
        if (job) {
            // Also stop a descendant that survives a successful top-level exit.
            if (!TerminateJobObject(job.get(), ERROR_CANCELLED)) {
                job.reset(); // KILL_ON_JOB_CLOSE remains a final termination path.
                TerminateProcess(process.get(), ERROR_CANCELLED);
            }
        }
        // Assignment can fail while the just-created process is suspended.
        // It must still be terminated even when it never entered the job.
        if (WaitForSingleObject(process.get(), 0) != WAIT_OBJECT_0) {
            TerminateProcess(process.get(), ERROR_CANCELLED);
        }
        WaitForSingleObject(process.get(), INFINITE);
        for (const auto& owned : ownedProcesses) WaitForSingleObject(owned.get(), INFINITE);
        if (job) {
            JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting{};
            while (QueryInformationJobObject(job.get(), JobObjectBasicAccountingInformation,
                &accounting, sizeof(accounting), nullptr) && accounting.ActiveProcesses != 0) Sleep(10);
        }
        thread.reset();
        process.reset();
        job.reset();
    }
};

class AttributeList {
public:
    ~AttributeList() { if (list_) DeleteProcThreadAttributeList(list_); }
    bool Initialize(HANDLE* handles, std::wstring& error) {
        SIZE_T bytes = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
        storage_.resize(bytes);
        auto* candidate = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage_.data());
        if (!InitializeProcThreadAttributeList(candidate, 1, 0, &bytes)) {
            error = WindowsError(L"無法設定 AI 程序 / Cannot initialize AI process");
            return false;
        }
        list_ = candidate;
        if (!UpdateProcThreadAttribute(list_, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
            handles, 2 * sizeof(HANDLE), nullptr, nullptr)) {
            error = WindowsError(L"無法限制 AI 程序控制代碼 / Cannot restrict AI handles");
            return false;
        }
        return true;
    }
    LPPROC_THREAD_ATTRIBUTE_LIST get() const noexcept { return list_; }
private:
    std::vector<std::byte> storage_;
    LPPROC_THREAD_ATTRIBUTE_LIST list_ = nullptr;
};

bool Drain(HANDLE pipe, std::string& diagnostics) {
    std::array<char, 8192> buffer{};
    bool received = false;
    // A bounded drain lets the cancellation/timeout check run even under a
    // continuously writing child. The retained tail has a separate fixed cap.
    for (unsigned chunk = 0; chunk < 32; ++chunk) {
        DWORD available = 0;
        if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr) || !available) return received;
        DWORD count = 0;
        if (!ReadFile(pipe, buffer.data(), std::min<DWORD>(available,
            static_cast<DWORD>(buffer.size())), &count, nullptr) || !count) return received;
        received = true;
        diagnostics.append(buffer.data(), count);
        if (diagnostics.size() > MaximumDiagnosticBytes)
            diagnostics.erase(0, diagnostics.size() - MaximumDiagnosticBytes);
    }
    return received;
}

std::wstring DiagnosticText(const std::string& diagnostics) {
    if (diagnostics.empty()) return {};
    auto codePage = CP_UTF8;
    int length = MultiByteToWideChar(codePage, MB_ERR_INVALID_CHARS, diagnostics.data(),
        static_cast<int>(diagnostics.size()), nullptr, 0);
    if (length == 0) {
        codePage = CP_ACP;
        length = MultiByteToWideChar(codePage, 0, diagnostics.data(),
            static_cast<int>(diagnostics.size()), nullptr, 0);
    }
    std::wstring result(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(codePage, 0, diagnostics.data(), static_cast<int>(diagnostics.size()),
        result.data(), length);
    for (auto& character : result)
        if (character < L' ' && character != L'\n' && character != L'\t') character = L' ';
    return result;
}

constexpr std::uint64_t GiB = 1024ULL * 1024 * 1024;
constexpr std::uint64_t HardStagingBytes = 4 * GiB;

bool StopRequested(const ai::RuntimeOptions& options, const std::atomic_bool& cancelled,
    ULONGLONG started, std::wstring& error) {
    if (cancelled.load(std::memory_order_relaxed)) {
        error = L"AI 處理已取消。 / AI processing cancelled.";
        return true;
    }
    if (GetTickCount64() - started >= std::min(options.timeoutMs, 120000U)) {
        error = L"AI 處理逾時，已停止。 / AI processing timed out and was stopped.";
        return true;
    }
    if (options.stopReason) {
        auto reason = options.stopReason();
        if (!reason.empty()) { error = std::move(reason); return true; }
    }
    return false;
}

std::shared_ptr<TemporaryDirectory> OwnedDirectory(const std::filesystem::path& root) {
    GUID guid{};
    if (FAILED(CoCreateGuid(&guid))) throw std::runtime_error("Cannot create AI work identifier");
    std::array<wchar_t, 40> identifier{};
    StringFromGUID2(guid, identifier.data(), static_cast<int>(identifier.size()));
    const auto path = root / (L"ai-" + std::wstring(identifier.data()));
    if (!std::filesystem::create_directory(path)) throw std::runtime_error("Cannot create AI owned directory");
    return std::make_shared<TemporaryDirectory>(path);
}

std::wstring FileName(std::size_t index) {
    auto number = std::to_wstring(index);
    return std::wstring(4 - number.size(), L'0') + number + L".png";
}

// A PNG is complete only with a valid chunk envelope, CRCs and terminal IEND.
// WIC alone may accept truncated PNGs. The read handle also denies write opens
// while the same file is decoded and moved to its independent result lease.
bool CompletePng(HANDLE file, std::uint64_t maximumBytes, const std::function<bool()>& stop) {
    static const auto crcTable = [] {
        std::array<std::uint32_t, 256> table{};
        for (std::uint32_t index = 0; index < table.size(); ++index) {
            auto value = index;
            for (unsigned bit = 0; bit < 8; ++bit)
                value = (value >> 1) ^ (0xedb88320U & (0U - (value & 1U)));
            table[index] = value;
        }
        return table;
    }();
    LARGE_INTEGER length{};
    if (!GetFileSizeEx(file, &length) || length.QuadPart < 45 ||
        static_cast<std::uint64_t>(length.QuadPart) > maximumBytes) return false;
    auto read = [&](void* destination, DWORD bytes) {
        DWORD received = 0;
        return ReadFile(file, destination, bytes, &received, nullptr) && received == bytes;
    };
    constexpr std::array<unsigned char, 8> signature{137, 80, 78, 71, 13, 10, 26, 10};
    std::array<unsigned char, 8> actual{};
    if (!read(actual.data(), 8) || actual != signature) return false;
    auto bigEndian = [](const unsigned char* value) -> std::uint32_t {
        return (static_cast<std::uint32_t>(value[0]) << 24) |
            (static_cast<std::uint32_t>(value[1]) << 16) |
            (static_cast<std::uint32_t>(value[2]) << 8) | value[3];
    };
    std::uint64_t position = 8;
    bool header = false, pixels = false;
    std::array<unsigned char, 65536> buffer{};
    while (position + 12 <= static_cast<std::uint64_t>(length.QuadPart)) {
        if (stop()) return false;
        std::array<unsigned char, 8> chunk{};
        if (!read(chunk.data(), 8)) return false;
        const auto bytes = bigEndian(chunk.data());
        if (bytes > static_cast<std::uint64_t>(length.QuadPart) - position - 12) return false;
        const bool ihdr = std::equal(chunk.begin() + 4, chunk.end(), "IHDR");
        const bool idat = std::equal(chunk.begin() + 4, chunk.end(), "IDAT");
        const bool iend = std::equal(chunk.begin() + 4, chunk.end(), "IEND");
        if (!header && (!ihdr || bytes != 13)) return false;
        if (header && ihdr) return false;
        header = true;
        pixels = pixels || idat;
        std::uint32_t crc = 0xffffffffU;
        const auto update = [&](const unsigned char* data, std::size_t count) {
            for (std::size_t i = 0; i < count; ++i) {
                crc = (crc >> 8) ^ crcTable[(crc ^ data[i]) & 0xff];
            }
        };
        update(chunk.data() + 4, 4);
        std::uint32_t remaining = bytes;
        while (remaining) {
            if (stop()) return false;
            const auto count = std::min<DWORD>(remaining, static_cast<DWORD>(buffer.size()));
            if (!read(buffer.data(), count)) return false;
            update(buffer.data(), count);
            remaining -= count;
        }
        std::array<unsigned char, 4> expected{};
        if (!read(expected.data(), 4) || bigEndian(expected.data()) != (crc ^ 0xffffffffU)) return false;
        position += static_cast<std::uint64_t>(bytes) + 12;
        if (iend) return bytes == 0 && pixels && position == static_cast<std::uint64_t>(length.QuadPart);
    }
    return false;
}

bool CheckStaging(const std::filesystem::path& directory, std::uint64_t maximum, std::wstring& error) {
    std::uint64_t bytes = 0;
    std::size_t entries = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(directory)) {
        if (++entries > 128 || entry.is_symlink()) {
            error = L"AI 暫存檔案超限。 / AI staging file limit exceeded.";
            return false;
        }
        if (!entry.is_regular_file()) continue;
        const auto count = entry.file_size();
        if (count > maximum - bytes) {
            error = L"AI 暫存空間超限，已停止。 / AI staging space limit exceeded; processing stopped.";
            return false;
        }
        bytes += count;
    }
    return true;
}

bool Launch(const ai::RuntimeOptions& options, const std::filesystem::path& executable,
    const std::filesystem::path& models, const std::filesystem::path& input,
    const std::filesystem::path& output, int scale, Process& child,
    Handle& pipeRead, std::wstring& error) {
    child.job.reset(CreateJobObjectW(nullptr, nullptr));
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE |
        JOB_OBJECT_LIMIT_JOB_MEMORY | JOB_OBJECT_LIMIT_PROCESS_MEMORY;
    limits.JobMemoryLimit = static_cast<SIZE_T>(options.jobMemoryBytes);
    limits.ProcessMemoryLimit = static_cast<SIZE_T>(options.jobMemoryBytes);
    if (!child.job || !SetInformationJobObject(child.job.get(), JobObjectExtendedLimitInformation,
        &limits, sizeof(limits))) {
        error = WindowsError(L"無法限制 AI 記憶體 / Cannot configure AI memory limits");
        return false;
    }
    if (options.cpuPercent < 100) {
        JOBOBJECT_CPU_RATE_CONTROL_INFORMATION cpu{};
        cpu.ControlFlags = JOB_OBJECT_CPU_RATE_CONTROL_ENABLE | JOB_OBJECT_CPU_RATE_CONTROL_HARD_CAP;
        cpu.CpuRate = options.cpuPercent * 100;
        // Configure the Job before any child can run. Assignment of the
        // suspended child below also contains its descendants without breakaway.
        if (!SetInformationJobObject(child.job.get(), JobObjectCpuRateControlInformation,
            &cpu, sizeof(cpu))) {
            error = WindowsError(L"無法限制 AI CPU 使用率 / Cannot configure AI CPU limit");
            return false;
        }
    }
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    HANDLE read = nullptr, write = nullptr;
    if (!CreatePipe(&read, &write, &security, 65536)) {
        error = WindowsError(L"無法讀取 AI 診斷 / Cannot create AI diagnostic pipe");
        return false;
    }
    pipeRead.reset(read);
    Handle pipeWrite(write);
    if (!SetHandleInformation(pipeRead.get(), HANDLE_FLAG_INHERIT, 0)) {
        error = WindowsError(L"無法限制 AI 診斷控制代碼 / Cannot configure AI pipe");
        return false;
    }
    Handle inputHandle(CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
        &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!inputHandle) { error = WindowsError(L"無法建立 AI 標準輸入 / Cannot configure AI input"); return false; }
    HANDLE inheritedHandles[]{pipeWrite.get(), inputHandle.get()};
    AttributeList attributes;
    if (!attributes.Initialize(inheritedHandles, error)) return false;
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup.StartupInfo.wShowWindow = SW_HIDE;
    startup.StartupInfo.hStdInput = inputHandle.get();
    startup.StartupInfo.hStdOutput = pipeWrite.get();
    startup.StartupInfo.hStdError = pipeWrite.get();
    startup.lpAttributeList = attributes.get();
    auto command = Quote(executable.wstring()) + L" -i " + Quote(input.wstring()) +
        L" -o " + Quote(output.wstring()) + L" -m " + Quote(models.wstring()) +
        L" -s " + std::to_wstring(scale) + L" -n 0 -t " + std::to_wstring(options.tileSize) +
        L" -g " + std::to_wstring(options.gpu) + L" -j 1:1:1 -c 1 -f png";
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE,
        CREATE_SUSPENDED | CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS |
        EXTENDED_STARTUPINFO_PRESENT, nullptr, executable.parent_path().c_str(),
        &startup.StartupInfo, &process)) {
        error = WindowsError(L"無法啟動 AI 程序 / Cannot start AI process");
        return false;
    }
    child.process.reset(process.hProcess);
    child.thread.reset(process.hThread);
    if (!AssignProcessToJobObject(child.job.get(), child.process.get())) {
        error = WindowsError(L"無法管理 AI 子程序 / Cannot contain AI process");
        return false;
    }
    if (ResumeThread(child.thread.get()) == static_cast<DWORD>(-1)) {
        error = WindowsError(L"無法開始 AI 處理 / Cannot resume AI process");
        return false;
    }
    return true;
}
}

namespace ai {
bool ValidateOutputSize(const ImageData& source, int scale, std::wstring& error) {
    return ValidateOutputSize(source, scale, error, MaximumDecodeBytes);
}

bool ValidateOutputSize(const ImageData& source, int scale, std::wstring& error,
    std::uint64_t maximumBytes, unsigned maximumDimension) {
    error.clear();
    if (scale != 2 && scale != 4 && scale != 8) {
        error = L"AI 倍率必須是 2×、4× 或 8×。 / AI scale must be 2, 4 or 8.";
        return false;
    }
    const auto width = static_cast<std::uint64_t>(source.width) * scale;
    const auto height = static_cast<std::uint64_t>(source.height) * scale;
    maximumDimension = std::min(maximumDimension, 16384U);
    maximumBytes = std::min(maximumBytes, GiB);
    // Check dimensions first, so width*height*4 is bounded and cannot overflow.
    if (!width || !height || width > maximumDimension || height > maximumDimension ||
        width * height * 4 > maximumBytes) {
        error = scale == 8 ?
            L"AI 8× 輸出超過目前記憶體或顯示尺寸限制，請改用 4×。 / AI 8x output exceeds current memory or display limits; try 4x." :
            L"AI 輸出超過目前記憶體或顯示尺寸限制，請使用較低倍率。 / AI output exceeds current memory or display limits; use a lower scale.";
        return false;
    }
    return true;
}

void RunBatch(const RuntimeOptions& options, const std::vector<BatchInput>& inputs, int scale,
    const std::filesystem::path& workRoot, const std::atomic_bool& cancelled,
    const Completed& completed, std::wstring& error) {
    error.clear();
    const auto started = GetTickCount64();
    std::wstring phase = L"preflight";
    const auto stopped = [&] {
        if (!StopRequested(options, cancelled, started, error)) return false;
        if (error.find(L"timed out") != std::wstring::npos)
            error += L" [" + phase + L", " + std::to_wstring(GetTickCount64() - started) + L" ms]";
        return true;
    };
    try {
        if (stopped()) return;
        if (inputs.empty() || inputs.size() > 8 || !completed || options.gpu < 0 ||
            (options.tileSize != 128 && options.tileSize != 64) || options.timeoutMs == 0 ||
            options.cpuPercent == 0 || options.cpuPercent > 100 || options.jobMemoryBytes == 0 ||
            options.jobMemoryBytes > std::numeric_limits<SIZE_T>::max() || workRoot.empty()) {
            error = L"AI 批次或資源設定無效。 / Invalid AI batch or resource settings.";
            return;
        }
        const auto batchLimit = std::min(options.maximumBatchBytes, GiB);
        std::uint64_t finalBytes = 0, sourceBytes = 0;
        for (const auto& input : inputs) {
            if (!input.image || !ValidateOutputSize(*input.image, scale, error,
                options.maximumOutputBytes, options.maximumDimension)) {
                if (error.empty()) error = L"AI 來源不存在。 / AI source is missing.";
                return;
            }
            const auto& image = *input.image;
            const auto bytes = static_cast<std::uint64_t>(image.stride) * image.height;
            if (image.stride < static_cast<std::uint64_t>(image.width) * 4 ||
                !bytes || bytes > image.pixels.size() || image.pixels.size() > GiB) {
                error = L"AI 輸入像素資料無效。 / Invalid AI input pixel buffer.";
                return;
            }
            const auto outputBytes = static_cast<std::uint64_t>(image.width) * image.height * scale * scale * 4;
            if (outputBytes > batchLimit - finalBytes) {
                error = L"AI 批次輸出超過工作預算。 / AI batch output exceeds the work budget.";
                return;
            }
            finalBytes += outputBytes;
            sourceBytes += static_cast<std::uint64_t>(image.width) * image.height * 4;
        }
        // PNG uncompressed data plus conservative envelope/deflate overhead.
        // Include all inputs, intermediates, outputs and independently leased results.
        const auto stagingEstimate = sourceBytes + finalBytes + (scale == 8 ? finalBytes / 4 : 0) +
            std::min(finalBytes, static_cast<std::uint64_t>(inputs.size()) * 3 * 65536);
        const auto stagingLimit = std::min({options.stagingBytes, HardStagingBytes, finalBytes * 4});
        if (stagingEstimate > stagingLimit) {
            error = L"AI 暫存空間預算不足。 / AI staging space budget is insufficient.";
            return;
        }
        const auto executable = std::filesystem::absolute(options.executable);
        const auto models = std::filesystem::absolute(options.modelDirectory);
        if (options.executable.empty() || !std::filesystem::is_regular_file(executable)) {
            error = L"找不到本機 AI 執行檔。 / Local AI executable is missing."; return;
        }
        for (const auto stageScale : (scale == 8 ? std::vector<int>{4, 2} : std::vector<int>{scale})) {
            const auto stem = L"up" + std::to_wstring(stageScale) + L"x-no-denoise";
            if (options.modelDirectory.empty() || !std::filesystem::is_directory(models) ||
                !std::filesystem::is_regular_file(models / (stem + L".param")) ||
                !std::filesystem::is_regular_file(models / (stem + L".bin"))) {
                error = L"找不到本機 AI 無降噪模型。 / Local AI no-denoise model is missing."; return;
            }
        }
        // Multiple managers/callers still share a single contained child slot.
        // The same deadline includes waiting; cancellation is checked every 50ms.
        static std::timed_mutex childSlot;
        std::unique_lock<std::timed_mutex> slot(childSlot, std::defer_lock);
        phase = L"waiting for AI child slot";
        while (!slot.try_lock_for(std::chrono::milliseconds(50))) {
            if (stopped()) return;
        }
        if (stopped()) return;
        phase = L"COM startup";
        const auto comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        struct ComScope { HRESULT result; ~ComScope() { if (SUCCEEDED(result)) CoUninitialize(); } } comScope{comResult};
        if (FAILED(comResult) && comResult != RPC_E_CHANGED_MODE) {
            error = WindowsError(L"無法初始化 AI 圖片處理 / Cannot initialize AI imaging", static_cast<DWORD>(comResult)); return;
        }
        if (stopped()) return;
        const auto root = std::filesystem::absolute(workRoot);
        std::filesystem::create_directories(root);
        auto lease = OwnedDirectory(root);
        const auto inputDirectory = lease->path / L"input";
        const auto middleDirectory = lease->path / L"middle";
        const auto verifiedDirectory = lease->path / L"verified";
        const auto outputDirectory = lease->path / L"output";
        std::filesystem::create_directory(inputDirectory);
        std::filesystem::create_directory(outputDirectory);
        if (scale == 8) {
            std::filesystem::create_directory(middleDirectory);
            std::filesystem::create_directory(verifiedDirectory);
        }
        std::uint64_t publishedBytes = 0;
        const auto stagingHealthy = [&] {
            return CheckStaging(lease->path, stagingLimit - publishedBytes, error);
        };
        for (std::size_t index = 0; index < inputs.size(); ++index) {
            if (stopped()) return;
            phase = L"source PNG encoding #" + std::to_wstring(index);
            if (!ImageExporter::SavePng(*inputs[index].image, inputDirectory / FileName(index), 0, error)) return;
            if (stopped() || !stagingHealthy()) return;
        }
        const auto stage = [&](const std::filesystem::path& input, const std::filesystem::path& output,
            int modelScale, int sourceScale, bool finalStage) -> bool {
            std::vector<bool> accepted(inputs.size(), false);
            std::vector<std::wstring> invalid(inputs.size());
            const auto scan = [&]() -> bool {
                for (std::size_t index = 0; index < inputs.size(); ++index) {
                    if (accepted[index]) continue;
                    if (stopped()) return false;
                    const auto file = output / FileName(index);
                    Handle closed(CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE,
                        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
                    const auto maximumPng = std::min(stagingLimit, static_cast<std::uint64_t>(
                        inputs[index].image->width) * inputs[index].image->height * sourceScale * sourceScale * 8 + 65536);
                    if (!closed || !CompletePng(closed.get(), maximumPng,
                        [&] { return stopped(); })) continue;
                    phase = L"output PNG decode #" + std::to_wstring(index);
                    auto image = WicDecoder::Decode(file.wstring(), invalid[index], options.maximumOutputBytes);
                    if (!image) continue;
                    if (image->width != static_cast<std::uint64_t>(inputs[index].image->width) * sourceScale ||
                        image->height != static_cast<std::uint64_t>(inputs[index].image->height) * sourceScale) {
                        invalid[index] = L"AI 輸出尺寸不符。 / AI output dimensions do not match the requested scale.";
                        continue;
                    }
                    if (stopped()) return false;
                    if (finalStage) {
                        if (!stagingHealthy()) return false;
                        const auto resultBytes = std::filesystem::file_size(file);
                        auto resultLease = OwnedDirectory(root);
                        const auto destination = resultLease->path / L"output.png";
                        if (!MoveFileExW(file.c_str(), destination.c_str(), 0)) {
                            error = WindowsError(L"無法保存 AI 結果 / Cannot preserve AI result"); return false;
                        }
                        publishedBytes += resultBytes;
                        image->path = inputs[index].image->path;
                        image->sourceFingerprint = inputs[index].image->sourceFingerprint;
                        image->sourceWasJpegXr = inputs[index].image->sourceWasJpegXr;
                        image->hdrToneMappedToSdr = inputs[index].image->hdrToneMappedToSdr;
                        if (stopped()) return false;
                        accepted[index] = true;
                        completed(index, {std::move(image), {destination, std::move(resultLease)}});
                    } else {
                        if (!MoveFileExW(file.c_str(), (verifiedDirectory / FileName(index)).c_str(), 0)) {
                            error = WindowsError(L"無法保存 AI 中間圖 / Cannot preserve AI intermediate"); return false;
                        }
                        accepted[index] = true;
                    }
                }
                return true;
            };
            if (stopped()) return false;
            Process child; // Stops all descendants before batch lease cleanup, including exceptional exit.
            Handle diagnosticsPipe;
            phase = L"child startup";
            if (!Launch(options, executable, models, input, output, modelScale, child, diagnosticsPipe, error)) return false;
            if (stopped()) return false;
            phase = L"child inference";
            std::string diagnostics;
            DWORD exitCode = STILL_ACTIVE;
            ULONGLONG nextScan = 0, nextStaging = 0;
            for (;;) {
                const bool received = Drain(diagnosticsPipe.get(), diagnostics);
                if (stopped()) return false;
                const auto now = GetTickCount64();
                if (now >= nextStaging) {
                    if (!stagingHealthy()) return false;
                    nextStaging = now + 50;
                }
                if (now >= nextScan) {
                    if (!scan()) return false;
                    nextScan = now + 50;
                }
                const auto status = WaitForSingleObject(child.process.get(), received ? 1 : 10);
                if (status == WAIT_OBJECT_0) {
                    if (!GetExitCodeProcess(child.process.get(), &exitCode)) {
                        error = WindowsError(L"無法讀取 AI 結果 / Cannot read AI exit status"); return false;
                    }
                    break;
                }
                if (status != WAIT_TIMEOUT) {
                    error = WindowsError(L"無法等待 AI 程序 / Cannot wait for AI process"); return false;
                }
            }
            child.Stop();
            Drain(diagnosticsPipe.get(), diagnostics);
            if (!stagingHealthy() || !scan()) return false;
            if (exitCode != 0) {
                error = L"AI 處理失敗 / AI process failed (" + std::to_wstring(exitCode) + L")";
                const auto details = DiagnosticText(diagnostics);
                if (!details.empty()) error += L": " + details;
                return false;
            }
            for (std::size_t index = 0; index < accepted.size(); ++index) {
                if (!accepted[index]) {
                    error = invalid[index].empty() ? L"AI 未產生完整有效的 PNG 圖片。 / AI did not produce a complete valid PNG." : invalid[index];
                    return false;
                }
            }
            return true;
        };
        if (scale == 8) {
            if (!stage(inputDirectory, middleDirectory, 4, 4, false)) return;
            std::filesystem::remove_all(inputDirectory);
            if (!stopped())
                stage(verifiedDirectory, outputDirectory, 2, 8, true);
        } else stage(inputDirectory, outputDirectory, scale, scale, true);
    } catch (const std::exception&) {
        error = L"AI 檔案或記憶體處理失敗。 / AI file or memory operation failed.";
    } catch (...) {
        error = L"AI 處理未完成。 / AI processing did not complete.";
    }
}

Output Run(const RuntimeOptions& options, const ImageData& source, int scale,
    const std::filesystem::path& workRoot, const std::atomic_bool& cancelled, std::wstring& error) {
    Output result;
    // Synchronous, read-only alias avoids duplicating a large original buffer.
    auto alias = std::shared_ptr<ImageData>(const_cast<ImageData*>(&source), [](ImageData*) {});
    RunBatch(options, {{L"single", std::move(alias)}}, scale, workRoot, cancelled,
        [&](std::size_t, Output output) { result = std::move(output); }, error);
    if (cancelled.load(std::memory_order_relaxed)) return {};
    return result;
}
}
