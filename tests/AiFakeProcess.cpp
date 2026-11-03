#include "ImageExporter.h"
#include "WicDecoder.h"

#include <Windows.h>
#include <objbase.h>

#include <array>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {
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

void Write(const fs::path& path, const std::string& value) {
    std::ofstream(path, std::ios::binary) << value;
}

bool RecordJobLimits(const fs::path& path) {
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION memory{};
    JOBOBJECT_CPU_RATE_CONTROL_INFORMATION cpu{};
    if (!QueryInformationJobObject(nullptr, JobObjectExtendedLimitInformation,
        &memory, sizeof(memory), nullptr) ||
        !QueryInformationJobObject(nullptr, JobObjectCpuRateControlInformation,
            &cpu, sizeof(cpu), nullptr)) return false;
    std::ofstream record(path);
    record << cpu.ControlFlags << ' ' << cpu.CpuRate << ' '
        << memory.BasicLimitInformation.LimitFlags << ' ' << memory.JobMemoryLimit << ' '
        << memory.ProcessMemoryLimit << ' ' << GetPriorityClass(GetCurrentProcess()) << '\n';
    return record.good();
}
}

int wmain(int argc, wchar_t** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    const auto comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    struct ComScope { HRESULT result; ~ComScope() { if (SUCCEEDED(result)) CoUninitialize(); } } scope{comResult};
    if (argc == 3 && std::wstring(argv[1]) == L"--query-descendant")
        return RecordJobLimits(fs::path(argv[2]) / L"descendant-limits.txt") ? 0 : 73;
    if (argc == 3 && std::wstring(argv[1]) == L"--descendant") {
        const fs::path root(argv[2]);
        if (!RecordJobLimits(root / L"descendant-limits.txt")) return 73;
        const auto held = CreateFileW((root / L"held.txt").c_str(), GENERIC_WRITE, 0,
            nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (held == INVALID_HANDLE_VALUE) return 71;
        Write(root / L"descendant.pid", std::to_string(GetCurrentProcessId()));
        Sleep(INFINITE);
        return 72;
    }
    if (argc != 21) return 80;
    fs::path input, output, models;
    int scale = 0;
    bool denoise = false, tile = false, gpu = false, threads = false, sync = false;
    try {
        for (int i = 1; i + 1 < argc; i += 2) {
            const std::wstring flag(argv[i]), value(argv[i + 1]);
            if (flag == L"-i") input = value;
            else if (flag == L"-o") output = value;
            else if (flag == L"-m") models = value;
            else if (flag == L"-s") scale = std::stoi(value);
            else if (flag == L"-n") denoise = value == L"0";
            else if (flag == L"-t") tile = value == L"128" || value == L"64";
            else if (flag == L"-g") gpu = std::stoi(value) >= 0;
            else if (flag == L"-j") threads = value == L"1:1:1";
            else if (flag == L"-c") sync = value == L"1";
            else if (flag != L"-f" || value != L"png") return 81;
        }
    } catch (...) { return 82; }
    if (input.empty() || output.empty() || models.empty() ||
        (scale != 2 && scale != 4) || !denoise || !tile || !gpu || !threads || !sync) return 83;
    Write(models / L"launched.txt", "started");
    std::ofstream(models / L"launches.txt", std::ios::app) << scale << '\n';
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    if (!QueryInformationJobObject(nullptr, JobObjectExtendedLimitInformation,
        &limits, sizeof(limits), nullptr) ||
        !(limits.BasicLimitInformation.LimitFlags & JOB_OBJECT_LIMIT_JOB_MEMORY) ||
        !(limits.BasicLimitInformation.LimitFlags & JOB_OBJECT_LIMIT_PROCESS_MEMORY) ||
        (limits.BasicLimitInformation.LimitFlags & (JOB_OBJECT_LIMIT_BREAKAWAY_OK | JOB_OBJECT_LIMIT_SILENT_BREAKAWAY_OK)) ||
        GetPriorityClass(GetCurrentProcess()) != BELOW_NORMAL_PRIORITY_CLASS) return 89;
    if (!RecordJobLimits(models / L"child-limits.txt")) return 96;
    std::string mode;
    std::ifstream(models / L"mode.txt") >> mode;
    struct FileScope { HANDLE value = INVALID_HANDLE_VALUE; ~FileScope() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); } } active;
    if (mode == "serialization") {
        active.value = CreateFileW((models / L"active.lock").c_str(), GENERIC_READ | GENERIC_WRITE,
            0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (active.value == INVALID_HANDLE_VALUE) return 95;
        Sleep(250);
    }
    if (mode == "nonzero") {
        constexpr char text[] = "controlled inference failure";
        DWORD written = 0;
        WriteFile(GetStdHandle(STD_ERROR_HANDLE), text, sizeof(text) - 1, &written, nullptr);
        return 37;
    }
    if (mode == "none") return 0;
    if (mode == "memory") {
        const auto allocation = VirtualAlloc(nullptr, 128ULL * 1024 * 1024,
            MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        Write(models / L"memory.txt", allocation ? "unlimited" : "denied");
        if (allocation) VirtualFree(allocation, 0, MEM_RELEASE);
        return 93;
    }
    if (mode == "hang" || mode == "descendant" || mode == "job-query") {
        if (mode == "descendant" || mode == "job-query") {
            std::array<wchar_t, 32768> self{};
            GetModuleFileNameW(nullptr, self.data(), static_cast<DWORD>(self.size()));
            auto command = Quote(self.data()) +
                (mode == "job-query" ? L" --query-descendant " : L" --descendant ") + Quote(models.wstring());
            STARTUPINFOW startup{sizeof(startup)};
            PROCESS_INFORMATION process{};
            if (!CreateProcessW(self.data(), command.data(), nullptr, nullptr, FALSE,
                CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) return 84;
            CloseHandle(process.hThread);
            if (mode == "job-query") {
                DWORD exit = STILL_ACTIVE;
                const bool success = WaitForSingleObject(process.hProcess, 5000) == WAIT_OBJECT_0 &&
                    GetExitCodeProcess(process.hProcess, &exit) && exit == 0;
                CloseHandle(process.hProcess);
                if (!success) return 97;
            } else CloseHandle(process.hProcess);
        }
        if (mode != "job-query") {
            Sleep(INFINITE);
            return 85;
        }
    }
    if (mode == "flood") {
        std::array<char, 65536> buffer{};
        buffer.fill('z');
        for (unsigned i = 0; i < 512; ++i) {
            DWORD written = 0;
            if (!WriteFile(GetStdHandle(i % 2 ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE),
                buffer.data(), static_cast<DWORD>(buffer.size()), &written, nullptr)) return 86;
        }
    }
    std::vector<fs::path> inputs;
    const bool directory = fs::is_directory(input);
    if (directory) {
        for (const auto& entry : fs::directory_iterator(input)) inputs.push_back(entry.path());
        std::sort(inputs.begin(), inputs.end());
    } else inputs.push_back(input);
    if (mode == "second-fails" && scale == 2) return 37;
    if (mode == "stage-delay") Sleep(100);
    for (std::size_t index = 0; index < inputs.size(); ++index) {
        const auto destination = directory ? output / inputs[index].filename() : output;
        if (mode == "corrupt" || (mode == "partial-corrupt" && index == 1)) {
            Write(destination, "broken png"); continue;
        }
        std::wstring error;
        const auto source = WicDecoder::Decode(inputs[index].wstring(), error);
        if (!source) return 87;
        ImageData target;
        target.width = mode == "wrong" ? 1 : source->width * scale;
        target.height = mode == "wrong" ? 1 : source->height * scale;
        target.stride = target.width * 4;
        target.pixels.resize(static_cast<std::size_t>(target.stride) * target.height);
        for (std::uint32_t y = 0; y < target.height; ++y) {
            for (std::uint32_t x = 0; x < target.width; ++x) {
                const auto from = static_cast<std::size_t>(y / scale) * source->stride + (x / scale) * 4;
                const auto to = static_cast<std::size_t>(y) * target.stride + x * 4;
                for (unsigned channel = 0; channel < 4; ++channel)
                    target.pixels[to + channel] = source->pixels[from + channel];
            }
        }
        if (!ImageExporter::SavePng(target, destination, 0, error)) return 88;
        if ((mode == "progressive" || mode == "closed") && index == 0) {
            const auto held = CreateFileW(destination.c_str(), GENERIC_READ | GENERIC_WRITE,
                FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (held == INVALID_HANDLE_VALUE) return 94;
            Write(models / L"writer-open.txt", "open");
            Sleep(180);
            Write(models / L"writer-closed.txt", "closed");
            CloseHandle(held);
            Sleep(220);
        }
        if (mode == "partial" && index == 0) { Sleep(150); return 37; }
        if (mode == "cancel-after-first" && index == 0) Sleep(INFINITE);
    }
    if (mode == "progressive") Sleep(180);
    Write(models / L"finished.txt", "finished");
    return 0;
}
