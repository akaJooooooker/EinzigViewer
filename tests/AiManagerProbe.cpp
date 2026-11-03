#include "AiManager.h"
#include "ImageExporter.h"
#include "ImageFingerprint.h"
#include "ImageRotation.h"

#include <Windows.h>
#include <winioctl.h>

#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <functional>
#include <fstream>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>

using namespace std::chrono_literals;

namespace {
void Require(const bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

template<class Predicate>
void Wait(Predicate predicate, const char* message, const std::chrono::milliseconds timeout = 2500ms) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!predicate()) {
        if (std::chrono::steady_clock::now() >= deadline) throw std::runtime_error(message);
        std::this_thread::sleep_for(2ms);
    }
}

std::shared_ptr<ImageData> Image(const std::wstring& name, const unsigned marker = 7,
    const unsigned width = 2, const unsigned height = 2) {
    auto image = std::make_shared<ImageData>();
    image->path = name;
    image->width = width;
    image->height = height;
    image->stride = width * 4;
    image->pixels.resize(static_cast<std::size_t>(image->stride) * height, static_cast<std::byte>(marker));
    for (std::size_t offset = 3; offset < image->pixels.size(); offset += 4) image->pixels[offset] = std::byte{255};
    return image;
}

struct Lease {
    std::filesystem::path directory;
    ~Lease() { std::error_code ignored; std::filesystem::remove_all(directory, ignored); }
};

struct Fake : std::enable_shared_from_this<Fake> {
    struct Call {
        std::wstring source;
        int scale = 0;
        unsigned cpuPercent = 0;
        bool release = false;
        bool ignoreCancellation = false;
        bool exited = false;
        std::weak_ptr<ImageData> image;
        std::filesystem::path artifact;
    };
    std::mutex mutex;
    std::condition_variable changed;
    std::vector<std::shared_ptr<Call>> calls;
    bool automatic = false;
    bool ignoreCancellation = false;
    bool fail = false;
    std::uint64_t paddedBytes = 0;

    ai::Executor Executor() {
        return [self = shared_from_this()](const ai::RuntimeOptions& options, const ImageData& source,
            const int scale, const std::filesystem::path& root, const std::atomic_bool& cancelled,
            std::wstring& error) -> ai::Output {
            auto call = std::make_shared<Call>();
            std::size_t number = 0;
            {
                std::unique_lock lock(self->mutex);
                call->source = source.path;
                call->scale = scale;
                call->cpuPercent = options.cpuPercent;
                call->ignoreCancellation = self->ignoreCancellation;
                call->release = self->automatic;
                number = self->calls.size();
                self->calls.push_back(call);
                self->changed.notify_all();
                while (!call->release && (call->ignoreCancellation || !cancelled.load())) self->changed.wait_for(lock, 5ms);
            }
            struct Exit {
                std::shared_ptr<Fake> fake;
                std::shared_ptr<Call> call;
                ~Exit() { std::lock_guard lock(fake->mutex); call->exited = true; fake->changed.notify_all(); }
            } exit{self, call};
            if (cancelled.load() && !call->ignoreCancellation) return {};
            if (self->fail) { error = L"controlled failure"; return {}; }
            auto image = Image(source.path, std::to_integer<unsigned>(source.pixels.front()),
                source.width * scale, source.height * scale);
            const auto directory = root / (L"fixture-" + std::to_wstring(number));
            std::filesystem::create_directories(directory);
            const auto path = directory / L"result.png";
            auto lease = std::make_shared<Lease>();
            lease->directory = directory;
            if (!ImageExporter::SavePng(*image, path, 0, error)) return {};
            if (self->paddedBytes != 0) {
                const HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
                DWORD ignored = 0;
                LARGE_INTEGER end{};
                end.QuadPart = static_cast<LONGLONG>(self->paddedBytes);
                const bool sparse = file != INVALID_HANDLE_VALUE &&
                    DeviceIoControl(file, FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &ignored, nullptr) &&
                    SetFilePointerEx(file, end, nullptr, FILE_BEGIN) && SetEndOfFile(file);
                if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
                if (!sparse) { error = L"could not create sparse PNG fixture"; return {}; }
            }
            {
                std::lock_guard lock(self->mutex);
                call->image = image;
                call->artifact = path;
            }
            return {std::move(image), {path, std::move(lease)}};
        };
    }
    std::size_t Count() { std::lock_guard lock(mutex); return calls.size(); }
    std::shared_ptr<Call> At(const std::size_t index) { std::lock_guard lock(mutex); return calls.at(index); }
    void Release(const std::size_t index) { std::lock_guard lock(mutex); calls.at(index)->release = true; changed.notify_all(); }
    bool Exited(const std::size_t index) { std::lock_guard lock(mutex); return calls.at(index)->exited; }
};

void Idle(ai::Manager& manager, const std::chrono::milliseconds timeout = 2500ms) {
    Wait([&] { return !manager.Pending(); }, "manager did not become idle", timeout);
}

void InertConstruction(const std::filesystem::path& root) {
    auto fake = std::make_shared<Fake>();
    ai::Manager manager(nullptr, {}, root, fake->Executor());
    manager.SetRequests({}, 2);
    manager.Cancel();
    manager.Clear();
    Require(!manager.Pending() && fake->Count() == 0 && !std::filesystem::exists(root),
        "constructor/empty/off paths performed external work");
}

void DesiredOrderAndDuplicates(const std::filesystem::path& root) {
    auto fake = std::make_shared<Fake>();
    auto a = Image(L"A"), b = Image(L"B"), c = Image(L"C");
    ai::Manager manager(nullptr, {}, root, fake->Executor());
    manager.SetRequests({{L"B", b}, {L"A", a}, {L"B", b}, {L"C", c}}, 2);
    Wait([&] { return fake->Count() == 1; }, "first desired request was never launched");
    Require(fake->At(0)->source == L"B", "desired request priority was not preserved");
    manager.SetRequests({{L"B", b}, {L"C", c}, {L"A", a}, {L"B", b}}, 2);
    Require(manager.Pending(), "active request is not pending");
    fake->Release(0);
    Wait([&] { return fake->Count() == 2; }, "second desired request was never launched");
    Require(fake->At(1)->source == L"C", "new desired order was ignored");
    fake->Release(1);
    Wait([&] { return fake->Count() == 3; }, "third desired request was never launched");
    Require(fake->At(2)->source == L"A", "duplicate request was launched");
    fake->Release(2);
    Idle(manager);
    manager.SetRequests({{L"B", b}, {L"C", c}, {L"A", a}}, 2);
    Require(manager.TryGet(L"A", 2) && manager.TryGet(L"B", 2) && manager.TryGet(L"C", 2),
        "completed desired pages are not available");
    Idle(manager);
    Require(fake->Count() == 3, "completed work restarted");
}

void CancelAndCache(const std::filesystem::path& root) {
    auto fake = std::make_shared<Fake>();
    fake->ignoreCancellation = true;
    auto source = Image(L"cancel");
    ai::Manager manager(nullptr, {}, root, fake->Executor());
    manager.SetRequests({{L"page", source}}, 2);
    Wait([&] { return fake->Count() == 1; }, "cancel fixture never launched");
    manager.Cancel();
    Require(!manager.Pending() && !manager.TryGet(L"page", 2), "cancel did not immediately hide active result");
    fake->Release(0);
    Wait([&] { return fake->Exited(0); }, "cancel fixture did not exit");
    manager.SetRequests({{L"page", source}}, 2);
    Wait([&] { return fake->Count() == 2; }, "cancelled late result was incorrectly cached");
    Require(!manager.TryGet(L"page", 2), "late cancelled pixels became visible");
    fake->Release(1);
    Idle(manager);
    Require(manager.TryGet(L"page", 2) != nullptr, "replacement result was not cached");
    manager.Cancel();
    manager.SetRequests({{L"page", source}}, 2);
    Require(manager.TryGet(L"page", 2) != nullptr, "cancel discarded a completed cache");
    Idle(manager);
    Require(fake->Count() == 2, "completed cache was recomputed after cancel");
}

void ContentAndScale(const std::filesystem::path& root) {
    auto fake = std::make_shared<Fake>();
    fake->ignoreCancellation = true;
    auto old = Image(L"old", 11), fresh = Image(L"fresh", 22);
    ai::Manager manager(nullptr, {}, root, fake->Executor());
    manager.SetRequests({{L"same-key", old}}, 2);
    Wait([&] { return fake->Count() == 1; }, "old content never launched");
    manager.SetRequests({{L"same-key", fresh}}, 2);
    Require(!manager.TryGet(L"same-key", 2), "old source remained displayable after content replacement");
    fake->Release(0);
    Wait([&] { return fake->Count() == 2; }, "fresh content never launched");
    Require(fake->At(1)->source == L"fresh" && !manager.TryGet(L"same-key", 2), "old content published under new source identity");
    fake->Release(1);
    Idle(manager);
    Require(manager.TryGet(L"same-key", 2)->pixels.front() == std::byte{22}, "fresh content pixels were lost");
    manager.SetRequests({{L"same-key", fresh}}, 4);
    Require(!manager.TryGet(L"same-key", 2) && !manager.TryGet(L"same-key", 4), "scale switch displayed stale pixels");
    Wait([&] { return fake->Count() == 3; }, "new scale never launched");
    Require(fake->At(2)->scale == 4, "executor received wrong scale");
    fake->Release(2);
    Idle(manager);
    Require(manager.TryGet(L"same-key", 4)->width == 8, "new scale result dimensions are wrong");
}

void QueuedSourcesAreWeak(const std::filesystem::path& root) {
    auto fake = std::make_shared<Fake>();
    auto a = Image(L"A"), queued = Image(L"queued");
    std::weak_ptr<ImageData> weak = queued;
    ai::Manager manager(nullptr, {}, root, fake->Executor());
    manager.SetRequests({{L"A", a}, {L"queued", queued}}, 2);
    Wait([&] { return fake->Count() == 1; }, "weak queue fixture never launched");
    queued.reset();
    Require(weak.expired(), "queued source pinned original image memory");
    fake->Release(0);
    Idle(manager);
    Require(fake->Count() == 1, "expired queued source was executed");
}

void AliasedSourcesAreDifferentVersions(const std::filesystem::path& root) {
    struct Pair { ImageData first; ImageData second; };
    auto pair = std::make_shared<Pair>();
    pair->first = *Image(L"alias-first", 41);
    pair->second = *Image(L"alias-second", 42);
    auto first = std::shared_ptr<ImageData>(pair, &pair->first);
    auto second = std::shared_ptr<ImageData>(pair, &pair->second);
    auto fake = std::make_shared<Fake>();
    ai::Manager manager(nullptr, {}, root, fake->Executor());
    manager.SetRequests({{L"same-key", first}}, 2);
    Wait([&] { return fake->Count() == 1; }, "first aliased source never launched");
    fake->Release(0);
    Idle(manager);
    Require(manager.TryGet(L"same-key", 2)->pixels.front() == std::byte{41}, "first aliased output missing");
    manager.SetRequests({{L"same-key", second}}, 2);
    Require(!manager.TryGet(L"same-key", 2), "distinct aliased source reused old pixels");
    Wait([&] { return fake->Count() == 2; }, "distinct aliased source was not executed");
    fake->Release(1);
    Idle(manager);
    Require(manager.TryGet(L"same-key", 2)->pixels.front() == std::byte{42}, "second aliased pixels are wrong");
}

void ErrorsDoNotSpin(const std::filesystem::path& root) {
    auto fake = std::make_shared<Fake>();
    fake->automatic = true;
    fake->fail = true;
    auto source = Image(L"failure");
    ai::Manager manager(nullptr, {}, root, fake->Executor());
    manager.SetRequests({{L"page", source}}, 2);
    Wait([&] { return fake->Count() == 1; }, "failing request never launched");
    Idle(manager);
    Require(!manager.ErrorFor(L"page", 2).empty() && !manager.TryGet(L"page", 2), "failure reason was not exposed");
    manager.SetRequests({{L"page", source}}, 2);
    Idle(manager);
    Require(fake->Count() == 1, "failing request was retried without a content change");
    auto fresh = Image(L"new-failure");
    {
        std::lock_guard lock(fake->mutex);
        fake->automatic = false;
    }
    manager.SetRequests({{L"page", fresh}}, 2);
    Require(manager.ErrorFor(L"page", 2).empty(), "old error leaked into a new source version");
    Wait([&] { return fake->Count() == 2; }, "new source did not retry after failure");
    fake->Release(1);
    Idle(manager);
}

void MemoryLruAndDiskReload(const std::filesystem::path& root) {
    auto fake = std::make_shared<Fake>();
    fake->automatic = true;
    auto source = Image(L"large", 31, 2048, 4096);
    ai::Manager manager(nullptr, {}, root, fake->Executor());
    manager.SetRequests({{L"A", source}}, 2);
    Idle(manager, 15000ms);
    Require(fake->Count() == 1 && manager.TryGet(L"A", 2), "large A output was not cached");
    auto a = fake->At(0)->image;
    manager.SetRequests({{L"B", source}}, 2);
    Idle(manager, 15000ms);
    auto b = fake->At(1)->image;
    Require(!a.expired() && !b.expired(), "RAM cache evicted before its 256 MiB budget");
    manager.SetRequests({{L"A", source}, {L"B", source}}, 2);
    Require(manager.TryGet(L"A", 2) != nullptr, "LRU touch did not return A");
    manager.SetRequests({{L"C", source}}, 2);
    Idle(manager, 15000ms);
    Wait([&] { return b.expired(); }, "RAM LRU did not evict the least recently used 128 MiB output");
    Require(!a.expired(), "RAM LRU evicted the recently used output");
    manager.SetRequests({{L"B", source}}, 2);
    Wait([&] { return manager.TryGet(L"B", 2) != nullptr; }, "disk cache was not decoded after RAM eviction", 15000ms);
    Idle(manager);
    Require(fake->Count() == 3 && manager.TryGet(L"B", 2)->pixels.front() == std::byte{31},
        "RAM eviction recomputed inference or returned wrong disk pixels");
}

void DiskLru(const std::filesystem::path& root) {
    auto fake = std::make_shared<Fake>();
    fake->automatic = true;
    fake->paddedBytes = 256ULL * 1024 * 1024;
    auto source = Image(L"disk");
    ai::Manager manager(nullptr, {}, root, fake->Executor());
    manager.SetRequests({{L"A", source}}, 2);
    Idle(manager);
    manager.SetRequests({{L"B", source}}, 2);
    Idle(manager);
    const auto a = fake->At(0)->artifact, b = fake->At(1)->artifact;
    Require(std::filesystem::exists(a) && std::filesystem::exists(b), "disk cache evicted before its 512 MiB budget");
    manager.SetRequests({{L"A", source}, {L"B", source}}, 2);
    Require(manager.TryGet(L"A", 2) != nullptr, "disk LRU touch failed");
    manager.SetRequests({{L"C", source}}, 2);
    Idle(manager);
    Wait([&] { return !std::filesystem::exists(b); }, "disk LRU did not release the least recently used leased file");
    Require(std::filesystem::exists(a) && std::filesystem::exists(fake->At(2)->artifact),
        "disk LRU released the recently used result");
}

void StationaryViewportDoesNotReload(const std::filesystem::path& root) {
    auto fake=std::make_shared<Fake>();fake->automatic=true;
    auto source=Image(L"visible-large",31,2048,4096);
    ai::Manager manager(nullptr,{},root,fake->Executor());
    const std::vector<ai::Request> visible{{L"A",source},{L"B",source},{L"C",source}};
    manager.SetRequests(visible,2);Idle(manager,30000ms);
    Require(fake->Count()==3,"visible sources were not processed once");
    for(int frame=0;frame<20;++frame) {
        manager.SetRequests(visible,2);
        for(const auto& page:{L"A",L"B",L"C"})(void)manager.TryGet(page,2);
        Require(!manager.Pending(),"stationary paint requeued an evicted visible PNG");
        std::this_thread::sleep_for(2ms);
    }
    manager.SetRequests({{L"A",source}},2);
    Wait([&]{return manager.TryGet(L"A",2)!=nullptr;},"changed viewport did not reload cached PNG",15000ms);
    Idle(manager);Require(fake->Count()==3,"changed viewport repeated inference instead of disk reload");
}

void ClearDropsCacheAndLateResult(const std::filesystem::path& root) {
    auto fake = std::make_shared<Fake>();
    fake->ignoreCancellation = true;
    auto source = Image(L"clear");
    ai::Manager manager(nullptr, {}, root, fake->Executor());
    manager.SetRequests({{L"A", source}}, 2);
    Wait([&] { return fake->Count() == 1; }, "clear fixture never launched");
    fake->Release(0);
    Idle(manager);
    const auto artifact = fake->At(0)->artifact;
    manager.SetRequests({{L"B", source}}, 2);
    Wait([&] { return fake->Count() == 2; }, "clear active fixture never launched");
    manager.Clear();
    Require(!manager.Pending() && !manager.TryGet(L"A", 2) && !manager.TryGet(L"B", 2), "Clear kept a displayable cache or pending work");
    fake->Release(1);
    Wait([&] { return fake->Exited(1) && !std::filesystem::exists(artifact); }, "Clear did not clean completed cache");
    manager.SetRequests({{L"A", source}}, 2);
    Wait([&] { return fake->Count() == 3; }, "Clear did not invalidate completed cache");
    fake->Release(2);
    Idle(manager);
}

unsigned notifications = 0;
LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == ai::Manager::ReadyMessage) { ++notifications; return 0; }
    return DefWindowProcW(window, message, wParam, lParam);
}

void DestructionIsNonblocking(const std::filesystem::path& root) {
    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = WindowProcedure;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.lpszClassName = L"EinzigAiManagerProbe";
    RegisterClassW(&windowClass);
    const HWND window = CreateWindowExW(0, windowClass.lpszClassName, L"", 0,
        0, 0, 0, 0, HWND_MESSAGE, nullptr, windowClass.hInstance, nullptr);
    Require(window != nullptr, "notification fixture window failed");
    auto fake = std::make_shared<Fake>();
    fake->ignoreCancellation = true;
    auto source = Image(L"destructor");
    auto manager = std::make_unique<ai::Manager>(window, ai::RuntimeOptions{}, root, fake->Executor());
    manager->SetRequests({{L"A", source}}, 2);
    Wait([&] { return fake->Count() == 1; }, "destructor fixture never launched");
    const auto started = std::chrono::steady_clock::now();
    manager.reset();
    Require(std::chrono::steady_clock::now() - started < 100ms, "manager destructor waited for long-running executor");
    fake->Release(0);
    Wait([&] { return fake->Exited(0); }, "detached executor lost its state lifetime");
    std::this_thread::sleep_for(30ms);
    MSG message{};
    while (PeekMessageW(&message, window, 0, 0, PM_REMOVE)) DispatchMessageW(&message);
    DestroyWindow(window);
    Require(notifications == 0, "completion notified a window after manager destruction");
}

constexpr std::uint64_t GiB = 1024ULL * 1024 * 1024;
ai::ResourceSnapshot Healthy() {
    ai::ResourceSnapshot sample;
    sample.totalPhysical = 64 * GiB; sample.availablePhysical = 48 * GiB;
    sample.availableCommit = 48 * GiB; sample.memoryKnown = true;
    sample.diskTotal = 1000 * GiB; sample.diskAvailable = 300 * GiB; sample.diskKnown = true;
    sample.gpuTotal = 16 * GiB; sample.gpuAvailable = 12 * GiB; sample.gpuKnown = true;
    return sample;
}

struct Samples : std::enable_shared_from_this<Samples> {
    std::mutex mutex;
    ai::ResourceSnapshot sample = Healthy();
    unsigned reads = 0;
    ai::ResourceProbe Probe() {
        return [self = shared_from_this()](const std::filesystem::path&, int) {
            std::lock_guard lock(self->mutex); ++self->reads; return self->sample;
        };
    }
    void Set(ai::ResourceSnapshot value) { std::lock_guard lock(mutex); sample = value; }
    unsigned Reads() { std::lock_guard lock(mutex); return reads; }
};

struct BatchFake : std::enable_shared_from_this<BatchFake> {
    struct Call {
        std::vector<std::wstring> keys;
        std::vector<unsigned> markers;
        ai::RuntimeOptions options;
        int scale = 0;
        bool released = false, exited = false, cancelled = false;
    };
    std::mutex mutex;
    std::vector<std::shared_ptr<Call>> calls;
    bool automatic = true, partial = false, ignoreCancellation = false;
    std::wstring failure;
    std::uint64_t paddedBytes = 0;
    ai::BatchExecutor Executor() {
        return [self = shared_from_this()](const ai::RuntimeOptions& options,
            const std::vector<ai::BatchInput>& inputs, int scale, const std::filesystem::path& root,
            const std::atomic_bool& cancelled, const ai::Completed& completed, std::wstring& error) {
            auto call = std::make_shared<Call>();
            std::size_t number;
            bool partial;
            {
                std::lock_guard lock(self->mutex);
                call->options = options; call->scale = scale; call->released = self->automatic;
                for (const auto& input : inputs) {
                    call->keys.push_back(input.key);
                    call->markers.push_back(std::to_integer<unsigned>(input.image->pixels.front()));
                }
                number = self->calls.size(); self->calls.push_back(call); partial = self->partial;
            }
            auto emit = [&](std::size_t index) {
                const auto& source = *inputs[index].image;
                auto image = Image(source.path, std::to_integer<unsigned>(source.pixels.front()),
                    source.width * scale, source.height * scale);
                auto lease = std::make_shared<Lease>();
                lease->directory = root / (L"batch-" + std::to_wstring(number) + L"-" + std::to_wstring(index));
                std::filesystem::create_directories(lease->directory);
                auto path = lease->directory / L"result.png";
                if (!ImageExporter::SavePng(*image, path, 0, error)) return;
                if (self->paddedBytes) {
                    const HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
                    DWORD ignored = 0; LARGE_INTEGER end{}; end.QuadPart = self->paddedBytes;
                    const bool ok = file != INVALID_HANDLE_VALUE &&
                        DeviceIoControl(file, FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &ignored, nullptr) &&
                        SetFilePointerEx(file, end, nullptr, FILE_BEGIN) && SetEndOfFile(file);
                    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
                    Require(ok, "sparse batch artifact fixture failed");
                }
                completed(index, {std::move(image), {path, lease}});
            };
            if (partial && inputs.size() > 1) emit(0);
            for (;;) {
                { std::lock_guard lock(self->mutex); if (call->released) break; }
                if (cancelled.load() && !self->ignoreCancellation) break;
                if (options.stopReason && !self->ignoreCancellation) { const auto stop = options.stopReason(); if (!stop.empty()) { error = stop; break; } }
                std::this_thread::sleep_for(5ms);
            }
            if ((!cancelled.load() || self->ignoreCancellation) && error.empty()) {
                std::wstring failure;
                { std::lock_guard lock(self->mutex); failure = self->failure; }
                if (!failure.empty()) error = failure;
                else for (std::size_t i = partial && inputs.size() > 1 ? 1 : 0; i < inputs.size(); ++i) emit(i);
            }
            { std::lock_guard lock(self->mutex); call->cancelled = cancelled.load() || !error.empty(); call->exited = true; }
        };
    }
    std::size_t Count() { std::lock_guard lock(mutex); return calls.size(); }
    std::shared_ptr<Call> At(std::size_t i) { std::lock_guard lock(mutex); return calls.at(i); }
    void Release(std::size_t i) { std::lock_guard lock(mutex); calls.at(i)->released = true; }
    bool Exited(std::size_t i) { std::lock_guard lock(mutex); return calls.at(i)->exited; }
};

std::vector<ai::Page> Document(const std::filesystem::path& root, unsigned count,
    std::vector<std::shared_ptr<ImageData>>& sources, std::shared_ptr<std::atomic_uint> prepared = {}) {
    std::vector<ai::Page> pages;
    for (unsigned i = 0; i < count; ++i) {
        auto source = Image(L"source-" + std::to_wstring(i), i + 1);
        source->sourceFingerprint = ComputeImageFingerprint(*source);
        const auto path = root / (std::to_wstring(i) + L".png");
        std::filesystem::create_directories(root);
        std::wstring error; Require(ImageExporter::SavePng(*source, path, 0, error), "source fixture write failed");
        source->path = path.wstring();
        ai::Page page; page.key = std::to_wstring(i); page.path = path; page.decoded = source;
        if (prepared) page.prepare = [prepared](const std::wstring&, std::wstring&) { ++*prepared; return true; };
        pages.push_back(std::move(page)); sources.push_back(std::move(source));
    }
    return pages;
}

void LazyDocumentOrderAndBatches(const std::filesystem::path& root) {
    auto fake = std::make_shared<BatchFake>(); fake->automatic = false;
    auto samples = std::make_shared<Samples>(); auto prepared = std::make_shared<std::atomic_uint>(0);
    std::vector<std::shared_ptr<ImageData>> sources;
    auto pages = Document(root / L"sources", 13, sources, prepared);
    ai::Manager manager(nullptr, {}, root, {}, samples->Probe(), fake->Executor());
    manager.SetDocument(pages);
    std::this_thread::sleep_for(30ms);
    Require(fake->Count() == 0 && samples->Reads() == 0 && *prepared == 0,
        "registering an inactive document performed AI resource/source work");
    manager.SetPriority({{L"3", sources[3]}, {L"5", sources[5]}}, 2);
    Wait([&] { return fake->Count() == 1; }, "current document page did not start");
    Require(fake->At(0)->keys == std::vector<std::wstring>{L"3"}, "current page waited for a background batch");
    fake->Release(0);
    Wait([&] { return fake->Count() == 2; }, "background batch did not start");
    Require(fake->At(1)->keys == std::vector<std::wstring>{L"5",L"4",L"6",L"7",L"8",L"9",L"10",L"11"},
        "visible and forward document order or eight-page bound is wrong");
    fake->Release(1);
    Wait([&] { return fake->Count() == 3; }, "prior-page batch did not start");
    Require(fake->At(2)->keys == std::vector<std::wstring>{L"12",L"2",L"1",L"0"}, "remaining document/prior order is wrong");
    fake->Release(2); Idle(manager);
    Require(manager.Status().total == 13 && manager.Status().complete == 13 && manager.Status().failed == 0,
        "whole-document progress did not count independently completed pages");
    for (int frame = 0; frame < 10; ++frame) manager.SetPriority({{L"3", sources[3]}, {L"5", sources[5]}}, 2);
    Idle(manager); Require(fake->Count() == 3, "stationary priorities repeated background work");
}

void ProgressiveJumpAndPartialSuccess(const std::filesystem::path& root) {
    auto fake = std::make_shared<BatchFake>(); fake->automatic = false; fake->partial = true;
    auto samples = std::make_shared<Samples>(); std::vector<std::shared_ptr<ImageData>> sources;
    auto pages = Document(root / L"sources", 12, sources);
    ai::Manager manager(nullptr, {}, root, {}, samples->Probe(), fake->Executor());
    manager.SetDocument(pages); manager.SetPriority({{L"0", sources[0]}}, 2);
    Wait([&] { return fake->Count() == 1; }, "initial current did not start"); fake->Release(0);
    Wait([&] { return fake->Count() == 2 && manager.Status().complete >= 2; }, "progressive page was not published during batch");
    manager.SetPriority({{L"11", sources[11]}}, 2);
    fake->Release(1);
    Wait([&] { return fake->Count() == 3; }, "priority jump did not yield after its next completed page");
    Require(fake->At(2)->keys == std::vector<std::wstring>{L"11"}, "jump current page did not restart alone");
    Require(manager.Status().complete >= 3 && manager.TryGet(L"2",2)!=nullptr, "priority yield discarded its completed boundary page");
    fake->Release(2); manager.Cancel();
    Require(!manager.Pending(), "document Cancel remained pending");
}

void ViewportYieldsAfterCompletedPage(const std::filesystem::path& root) {
    auto fake=std::make_shared<BatchFake>();fake->automatic=false;
    auto samples=std::make_shared<Samples>();std::vector<std::shared_ptr<ImageData>> sources;
    auto pages=Document(root/L"sources",12,sources);
    ai::Manager manager(nullptr,{},root,{},samples->Probe(),fake->Executor());
    manager.SetDocument(pages);manager.SetPriority({{L"0",sources[0]}},2);
    Wait([&]{return fake->Count()==1;},"yield fixture did not enter model execution");
    manager.SetPriority({{L"10",sources[10]}},2);manager.SetPriority({{L"11",sources[11]}},2);
    std::this_thread::sleep_for(80ms);
    Require(fake->Count()==1 && !fake->Exited(0),"scrolling discarded a model page before any completed output");
    fake->Release(0);
    Wait([&]{return fake->Count()==2;},"new viewport did not run after the completed-page boundary");
    Require(manager.TryGet(L"0",2)!=nullptr && manager.Status().complete>=1,"page-boundary yield lost the completed result");
    Require(fake->At(1)->keys==std::vector<std::wstring>{L"11"},"yield did not use the newest visible destination");
    manager.Cancel();Wait([&]{return fake->Exited(1);},"explicit AI-off waited for another output boundary");
}

void ViewportReturnWithdrawsYield(const std::filesystem::path& root) {
    auto fake=std::make_shared<BatchFake>();fake->automatic=false;
    auto samples=std::make_shared<Samples>();std::vector<std::shared_ptr<ImageData>> sources;
    auto pages=Document(root/L"sources",12,sources);
    ai::Manager manager(nullptr,{},root,{},samples->Probe(),fake->Executor());
    manager.SetDocument(pages);manager.SetPriority({{L"0",sources[0]}},2);
    Wait([&]{return fake->Count()==1;},"withdraw-yield fixture did not enter model execution");
    manager.SetPriority({{L"11",sources[11]}},2);manager.SetPriority({{L"0",sources[0]}},2);
    fake->Release(0);Wait([&]{return fake->Count()==2;},"normal background work did not follow withdrawn yield");
    Require(!fake->At(0)->cancelled && manager.TryGet(L"0",2)!=nullptr,"returning into the current batch still cancelled its result");
    manager.Cancel();Wait([&]{return fake->Exited(1);},"withdraw-yield cleanup did not stop");
}

void ViewportCancelsDuringSourcePreparation(const std::filesystem::path& root) {
    auto fake=std::make_shared<BatchFake>();fake->automatic=false;
    auto samples=std::make_shared<Samples>();std::vector<std::shared_ptr<ImageData>> sources;
    auto pages=Document(root/L"sources",2,sources);sources.clear();
    auto entered=std::make_shared<std::atomic_bool>(false),release=std::make_shared<std::atomic_bool>(false);
    pages[0].prepare=[entered,release](const auto&,auto&){entered->store(true);while(!release->load())std::this_thread::sleep_for(5ms);return true;};
    ai::Manager manager(nullptr,{},root,{},samples->Probe(),fake->Executor());
    manager.SetDocument(pages);manager.SetPriority({{L"0",{}}},2);
    Wait([&]{return entered->load();},"prepare-yield fixture did not block preparation");
    manager.SetPriority({{L"1",{}}},2);release->store(true);
    Wait([&]{return fake->Count()==1;},"new viewport did not replace preparation");
    Require(fake->At(0)->keys==std::vector<std::wstring>{L"1"},"outdated prepared page started a model before new demand");
    manager.Cancel();Wait([&]{return fake->Exited(0);},"prepare-yield cleanup did not stop");
}

void StableDocumentIdentity(const std::filesystem::path& root) {
    auto fake = std::make_shared<BatchFake>(); auto samples = std::make_shared<Samples>();
    std::vector<std::shared_ptr<ImageData>> sources; auto pages = Document(root / L"sources", 1, sources);
    ai::Manager manager(nullptr, {}, root, {}, samples->Probe(), fake->Executor());
    manager.SetDocument(pages); manager.SetPriority({{L"0", sources[0]}}, 2); Idle(manager);
    sources.clear(); Require(pages[0].decoded.expired(), "document scheduler pinned decoded original after work");
    auto same = Image(pages[0].path.wstring(), 1); same->sourceFingerprint = ComputeImageFingerprint(*same);
    manager.SetPriority({{L"0", same}}, 2); Idle(manager);
    Require(fake->Count() == 1 && manager.TryGet(L"0", 2), "identical redecoded source invalidated stable cache");
    auto fresh = Image(pages[0].path.wstring(), 92); fresh->sourceFingerprint = ComputeImageFingerprint(*fresh);
    manager.SetPriority({{L"0", fresh}}, 2);
    Require(!manager.TryGet(L"0", 2), "source-content replacement exposed stale AI pixels");
    Idle(manager); Require(fake->Count() == 2 && manager.TryGet(L"0", 2)->pixels.front() == std::byte{92},
        "content replacement did not compute new AI pixels");
    manager.Cancel(); manager.SetPriority({{L"0", fresh}}, 2); Idle(manager);
    Require(fake->Count() == 2, "document cancel discarded completed cache");
    manager.SetPriority({{L"0", fresh}}, 8); Idle(manager);
    Require(fake->Count() == 3 && manager.TryGet(L"0", 8)->width == 16, "document scale8 failed");
}

void DocumentErrorsRequireReactivation(const std::filesystem::path& root) {
    auto fake = std::make_shared<BatchFake>(); fake->failure = L"controlled failure";
    auto samples = std::make_shared<Samples>(); std::vector<std::shared_ptr<ImageData>> sources;
    auto pages = Document(root / L"sources", 1, sources);
    ai::Manager manager(nullptr, {}, root, {}, samples->Probe(), fake->Executor());
    manager.SetDocument(pages); manager.SetPriority({{L"0", sources[0]}}, 2); Idle(manager);
    Require(manager.Status().failed == 1 && manager.Status().complete == 0 && !manager.ErrorFor(L"0", 2).empty(),
        "failed document page was counted complete or lost its reason");
    for (int i = 0; i < 10; ++i) manager.SetPriority({{L"0", sources[0]}}, 2);
    Idle(manager); Require(fake->Count() == 1, "stationary failed page retried");
    manager.Cancel(); manager.SetPriority({{L"0", sources[0]}}, 2); Idle(manager);
    Require(fake->Count() == 2, "explicit document reactivation did not retry failed page");
}

void DocumentPressureAndRecovery(const std::filesystem::path& root) {
    auto fake = std::make_shared<BatchFake>(); fake->automatic = false;
    auto samples = std::make_shared<Samples>(); std::vector<std::shared_ptr<ImageData>> sources;
    auto pages = Document(root / L"sources", 2, sources);
    ai::Manager manager(nullptr, {}, root, {}, samples->Probe(), fake->Executor());
    manager.SetDocument(pages); manager.SetPriority({{L"0", sources[0]}}, 2);
    Wait([&] { return fake->Count() == 1; }, "watchdog fixture did not start");
    auto low = Healthy(); low.availableCommit = GiB; samples->Set(low);
    Wait([&] { return manager.Status().paused && fake->Exited(0); }, "watchdog did not pause active batch", 3500ms);
    Require(!manager.Status().reason.empty() && manager.Status().complete == 0, "resource pause lost reason/progress");
    samples->Set(Healthy()); const auto resumed = std::chrono::steady_clock::now();
    std::this_thread::sleep_for(1800ms); Require(fake->Count() == 1, "resource recovery skipped three spaced samples");
    Wait([&] { return fake->Count() == 2; }, "resource recovery never resumed", 3500ms);
    Require(std::chrono::steady_clock::now() - resumed >= 2s && fake->At(1)->keys.size() == 1,
        "resource recovery did not restart conservatively");
    manager.Cancel();
}

void DocumentResourceRetryBound(const std::filesystem::path& root) {
    auto fake = std::make_shared<BatchFake>(); fake->failure = L"out of memory / GPU resource exhausted";
    auto samples = std::make_shared<Samples>(); std::vector<std::shared_ptr<ImageData>> sources;
    auto pages = Document(root / L"sources", 1, sources);
    ai::Manager manager(nullptr, {}, root, {}, samples->Probe(), fake->Executor());
    manager.SetDocument(pages); manager.SetPriority({{L"0", sources[0]}}, 2);
    Wait([&] { return fake->Count() == 2 && !manager.Pending(); }, "conservative resource retry never finished", 6500ms);
    Require(fake->At(0)->options.tileSize == 128 && fake->At(1)->options.tileSize == 64 &&
        fake->At(1)->keys.size() == 1 && manager.Status().failed == 1,
        "resource failure did not get exactly one tile64 single-page retry");
    std::this_thread::sleep_for(50ms); Require(fake->Count() == 2, "resource failure retried without bound");
}

void LazyPreparationAndNonblockingTeardown(const std::filesystem::path& root) {
    auto fake = std::make_shared<BatchFake>(); auto samples = std::make_shared<Samples>();
    std::vector<std::shared_ptr<ImageData>> sources; auto pages = Document(root / L"sources", 1, sources);
    sources.clear();
    auto entered = std::make_shared<std::atomic_bool>(false), release = std::make_shared<std::atomic_bool>(false);
    auto lease = std::make_shared<int>(1); std::weak_ptr<int> weak = lease;
    pages[0].lease = lease; pages[0].prepare = [entered, release](const std::wstring&, std::wstring&) {
        entered->store(true); while (!release->load()) std::this_thread::sleep_for(2ms); return true;
    };
    auto manager = std::make_unique<ai::Manager>(nullptr, ai::RuntimeOptions{}, root,
        ai::Executor{}, samples->Probe(), fake->Executor());
    manager->SetDocument(std::move(pages)); lease.reset(); manager->SetPriority({{L"0", {}}}, 2);
    Wait([&] { return entered->load(); }, "lazy source prepare was never invoked");
    const auto start = std::chrono::steady_clock::now(); manager.reset();
    Require(std::chrono::steady_clock::now() - start < 100ms && !weak.expired(), "teardown blocked or dropped active source lease");
    release->store(true);
    Wait([&] { return weak.expired(); }, "detached source lease leaked after preparation completed");
    Require(fake->Count() == 0, "cancelled preparation launched AI child");
}

void UpdatedDocumentSnapshotInvalidatesContent(const std::filesystem::path& root) {
    auto fake = std::make_shared<BatchFake>(); auto samples = std::make_shared<Samples>();
    std::vector<std::shared_ptr<ImageData>> sources; auto pages = Document(root / L"sources", 1, sources);
    ai::Manager manager(nullptr, {}, root, {}, samples->Probe(), fake->Executor());
    manager.SetDocument(pages); manager.SetPriority({{L"0", sources[0]}}, 2); Idle(manager);
    manager.Cancel(); auto fresh = Image(pages[0].path.wstring(), 73);
    fresh->sourceFingerprint = ComputeImageFingerprint(*fresh); pages[0].decoded = fresh;
    manager.SetDocument(pages); manager.SetPriority({{L"0", fresh}}, 2);
    Require(!manager.TryGet(L"0", 2), "updated document snapshot reused different content");
    Idle(manager); Require(fake->Count() == 2 && manager.TryGet(L"0", 2)->pixels.front() == std::byte{73},
        "updated document snapshot never computed fresh content");
}

void DocumentRotationAndPreset(const std::filesystem::path& root) {
    auto fake = std::make_shared<BatchFake>(); auto samples = std::make_shared<Samples>();
    auto source = Image(L"base", 17, 2, 3); source->sourceFingerprint = ComputeImageFingerprint(*source);
    const auto path = root / L"base.png"; std::filesystem::create_directories(root);
    std::wstring error; Require(ImageExporter::SavePng(*source, path, 0, error), "rotation fixture write failed");
    ai::Page page; page.key = L"rotated"; page.path = path; page.rotation = 1;
    ai::RuntimeOptions options; options.tileSize = 64;
    ai::Manager manager(nullptr, options, root, {}, samples->Probe(), fake->Executor());
    manager.SetDocument({page}); manager.SetPriority({{L"rotated", {}}}, 2); Idle(manager);
    auto result = manager.TryGet(L"rotated", 2);
    Require(result && result->width == 6 && result->height == 4 && fake->At(0)->options.tileSize == 64 &&
        result->sourceFingerprint == source->sourceFingerprint, "lazy rotation or explicit tile64 preset identity failed");
    auto rotated = RotateImage(*source, 1, error); rotated->path = path.wstring();
    manager.Cancel(); page.decoded = rotated; manager.SetDocument({page}); manager.SetPriority({{L"rotated", rotated}}, 2); Idle(manager);
    Require(fake->Count() == 1 && manager.TryGet(L"rotated", 2)->width == 6,
        "decoded source was rotated twice or lost stable preset cache");
}

void DocumentQuotaPausesWithoutSpin(const std::filesystem::path& root) {
    auto fake = std::make_shared<BatchFake>(); fake->paddedBytes = 3000;
    auto samples = std::make_shared<Samples>(); auto sample = Healthy();
    sample.diskTotal = 100 * GiB; sample.diskAvailable = 5 * GiB + 4096; samples->Set(sample);
    std::vector<std::shared_ptr<ImageData>> sources; auto pages = Document(root / L"sources", 12, sources);
    ai::Manager manager(nullptr, {}, root, {}, samples->Probe(), fake->Executor());
    manager.SetDocument(pages); manager.SetPriority({{L"0", sources[0]}}, 2);
    Wait([&] { return manager.Status().paused && manager.TryGet(L"0", 2); }, "disk quota never paused after retaining current result");
    Require(fake->Count() == 1 && manager.Status().complete == 1, "quota launched an oversized background batch");
    for (int frame = 0; frame < 12; ++frame) { manager.SetPriority({{L"0", sources[0]}}, 2); (void)manager.TryGet(L"0", 2); }
    std::this_thread::sleep_for(60ms); Require(fake->Count() == 1, "quota stationary viewport recomputed work");
    const auto previous = root / L"batch-0-0" / L"result.png";
    manager.SetPriority({{L"11", sources[11]}}, 2);
    Wait([&] { return manager.TryGet(L"11", 2) != nullptr; }, "current urgency did not reclaim nonvisible disk result");
    Wait([&] { return !std::filesystem::exists(previous); }, "retired artifact lease was not released on worker");
    Require(fake->Count() >= 2 && !std::filesystem::exists(previous) &&
        std::filesystem::exists(root / L"batch-1-0" / L"result.png"), "cache artifacts did not have independent quota leases");
    manager.Cancel();
}

void DocumentOutputBoundAndUnknownGpu(const std::filesystem::path& root) {
    auto fake = std::make_shared<BatchFake>(); fake->failure = L"controlled failure";
    auto samples = std::make_shared<Samples>(); auto sample = Healthy();
    sample.totalPhysical = 8 * GiB; sample.availablePhysical = 7 * GiB; samples->Set(sample);
    std::vector<std::shared_ptr<ImageData>> sources; std::vector<ai::Page> pages;
    for (unsigned i = 0; i < 6; ++i) {
        auto image = Image(L"bounded-" + std::to_wstring(i), i + 1, 1500, 1500);
        image->sourceFingerprint = ComputeImageFingerprint(*image);
        ai::Page page; page.key = std::to_wstring(i); page.decoded = image;
        pages.push_back(page); sources.push_back(std::move(image));
    }
    ai::Manager manager(nullptr, {}, root, {}, samples->Probe(), fake->Executor());
    manager.SetDocument(pages); manager.SetPriority({{L"0", sources[0]}}, 2); Idle(manager, 5000ms);
    Require(fake->Count() == 3 && fake->At(0)->keys.size() == 1 && fake->At(1)->keys.size() == 3 &&
        fake->At(2)->keys.size() == 2 && fake->At(1)->options.maximumBatchBytes == 128ULL * 1024 * 1024,
        "batch exceeded the independent output/transient budget");
    manager.Clear(); sample = Healthy(); sample.gpuKnown = false; samples->Set(sample);
    auto small = Image(L"small"), oversized = Image(L"oversized", 31, 3000, 3000);
    ai::Page a; a.key = L"small"; a.decoded = small;
    ai::Page b; b.key = L"oversized"; b.decoded = oversized;
    manager.SetDocument({a,b}); manager.SetPriority({{L"small",small}},4); Idle(manager,5000ms);
    Require(fake->Count() == 4 && fake->At(3)->keys.size() == 1 && manager.Status().failed == 2 &&
        !manager.ErrorFor(L"oversized",4).empty(), "unknown GPU admitted oversized output or a multipage batch");
}

void RunningBatchDoesNotSpendTheoreticalMaximum(const std::filesystem::path& root) {
    auto fake = std::make_shared<BatchFake>(); fake->automatic = false;
    auto samples = std::make_shared<Samples>(); std::vector<std::shared_ptr<ImageData>> sources;
    auto pages = Document(root / L"sources", 1, sources);
    ai::Manager manager(nullptr, {}, root, {}, samples->Probe(), fake->Executor());
    manager.SetDocument(pages); manager.SetPriority({{L"0", sources[0]}}, 2);
    Wait([&] { return fake->Count() == 1; }, "live budget fixture did not start");
    auto occupied = Healthy(); occupied.availablePhysical = 28 * GiB; occupied.gpuAvailable = 4 * GiB;
    samples->Set(occupied);
    std::this_thread::sleep_for(1400ms);
    Require(!manager.Status().paused && !fake->Exited(0) && fake->Count() == 1,
        "watchdog charged a tiny active batch for the unused theoretical maximum");
    fake->Release(0); Idle(manager);
    Require(manager.TryGet(L"0", 2) != nullptr && manager.Status().complete == 1,
        "healthy actual batch did not survive its own resource allocation");
}

void FreshPreflightShrinksPreparedBatch(const std::filesystem::path& root) {
    auto fake = std::make_shared<BatchFake>(); auto samples = std::make_shared<Samples>();
    std::vector<std::shared_ptr<ImageData>> sources; auto pages = Document(root / L"sources", 5, sources);
    for (std::size_t i = 1; i < pages.size(); ++i) {
        pages[i].prepare = [samples](const std::wstring&, std::wstring&) {
            auto sample = Healthy(); sample.gpuKnown = false; samples->Set(sample); return true;
        };
    }
    auto current = sources.front(); sources.clear();
    ai::Manager manager(nullptr, {}, root, {}, samples->Probe(), fake->Executor());
    manager.SetDocument(pages); manager.SetPriority({{L"0", current}}, 2); Idle(manager, 5000ms);
    Require(fake->Count() == 5 && manager.Status().complete == 5, "fresh unknown-GPU preflight did not defer prepared pages");
    for (std::size_t i = 1; i < fake->Count(); ++i)
        Require(fake->At(i)->keys.size() == 1, "GPU telemetry disappeared during prepare but runtime still received a multipage batch");
}

void PreparePressureStopsBeforeDecode(const std::filesystem::path& root) {
    auto fake = std::make_shared<BatchFake>(); auto samples = std::make_shared<Samples>();
    std::filesystem::create_directories(root); const auto path = root / L"invalid-source.png";
    { std::ofstream file(path, std::ios::binary); file << "invalid PNG must not be decoded after pressure"; }
    ai::Page page; page.key = L"blocked"; page.path = path;
    page.prepare = [samples](const std::wstring&, std::wstring&) {
        auto low = Healthy(); low.availableCommit = GiB; samples->Set(low); return true;
    };
    ai::Manager manager(nullptr, {}, root, {}, samples->Probe(), fake->Executor());
    manager.SetDocument({page}); manager.SetPriority({{L"blocked",{}}},2);
    Wait([&] { return manager.Status().paused; }, "pressure introduced by source preparation was not checked before decode");
    Require(manager.Status().failed == 0 && fake->Count() == 0,
        "worker decoded an invalid source or started inference after preparation exhausted resources");
    manager.Cancel();
}

void FreshRamBudgetBoundsRetainedSources(const std::filesystem::path& root) {
    auto fake = std::make_shared<BatchFake>(); fake->failure = L"controlled failure";
    auto reads = std::make_shared<std::atomic_uint>(0);
    const ai::ResourceProbe probe = [reads](const std::filesystem::path&,int) {
        auto sample = Healthy();
        if (++*reads >= 4) { sample.totalPhysical = 8 * GiB; sample.availablePhysical = 7 * GiB; }
        return sample;
    };
    std::vector<std::shared_ptr<ImageData>> sources; std::vector<ai::Page> pages;
    for (unsigned i=0;i<6;++i) {
        auto image = Image(L"ram-"+std::to_wstring(i),i+1,1500,1500);
        image->sourceFingerprint=ComputeImageFingerprint(*image);
        ai::Page page;page.key=std::to_wstring(i);page.decoded=image;pages.push_back(page);sources.push_back(image);
    }
    ai::Manager manager(nullptr,{},root,{},probe,fake->Executor());
    manager.SetDocument(pages);manager.SetPriority({{L"0",sources[0]}},2);Idle(manager,5000ms);
    Require(fake->Count()==3 && fake->At(1)->keys.size()==3 && fake->At(2)->keys.size()==2 &&
        manager.Status().limits.transientBytes==128ULL*1024*1024 && manager.Status().failed==6,
        "prepared batch retained output/source allocations beyond its fresh RAM portions");
}

void LazyPageThatFitsAloneIsDeferred(const std::filesystem::path& root) {
    auto fake=std::make_shared<BatchFake>();auto samples=std::make_shared<Samples>();
    auto sample=Healthy();sample.totalPhysical=8*GiB;sample.availablePhysical=7*GiB;samples->Set(sample);
    std::filesystem::create_directories(root);std::vector<ai::Page> pages;
    std::shared_ptr<ImageData> current;
    for(unsigned i=0;i<5;++i) {
        const unsigned side=i==0?2:i==4?2700:1200;
        auto image=Image(L"deferred-"+std::to_wstring(i),i+1,side,side);
        image->sourceFingerprint=ComputeImageFingerprint(*image);
        const auto path=root/(std::to_wstring(i)+L".png");std::wstring error;
        Require(ImageExporter::SavePng(*image,path,0,error),"deferred source fixture write failed");
        image->path=path.wstring();ai::Page page;page.key=std::to_wstring(i);page.path=path;page.decoded=image;
        pages.push_back(page);if(i==0)current=image;
    }
    ai::Manager manager(nullptr,{},root,{},samples->Probe(),fake->Executor());
    manager.SetDocument(pages);manager.SetPriority({{L"0",current}},2);Idle(manager,15000ms);
    Require(fake->Count()==3 && fake->At(1)->keys==std::vector<std::wstring>{L"1",L"2",L"3"} &&
        fake->At(2)->keys==std::vector<std::wstring>{L"4"} && manager.Status().failed==0 && manager.Status().complete==5,
        "lazy page was permanently failed by a previous page's remaining transient decode cap");
}

void LiveBudgetShrinksCompletedCache(const std::filesystem::path& root) {
    auto fake=std::make_shared<BatchFake>();auto samples=std::make_shared<Samples>();
    std::vector<std::shared_ptr<ImageData>> sources;std::vector<ai::Page> pages;
    for(unsigned i=0;i<10;++i) {
        auto source=Image(L"live-cache-"+std::to_wstring(i),i+1,i==9?2:2048,i==9?2:1024);
        source->sourceFingerprint=ComputeImageFingerprint(*source);
        ai::Page page;page.key=std::to_wstring(i);page.decoded=source;pages.push_back(page);sources.push_back(source);
    }
    const auto execute=fake->Executor();
    ai::BatchExecutor controlled=[fake,execute](const auto& options,const auto& inputs,int scale,const auto& directory,
        const auto& cancelled,const auto& complete,auto& error) {
        if(inputs.front().key==L"9") {std::lock_guard lock(fake->mutex);fake->automatic=false;}
        execute(options,inputs,scale,directory,cancelled,complete,error);
    };
    ai::Manager manager(nullptr,{},root,{},samples->Probe(),controlled);
    manager.SetDocument(pages);manager.SetPriority({{L"0",sources[0]}},2);
    Wait([&]{return fake->Count()==3;},"cache shrink fixture did not reach tiny active batch",15000ms);
    manager.SetPriority({{L"9",sources[9]}},2);
    auto occupied=Healthy();occupied.availablePhysical=11*GiB;samples->Set(occupied);
    const auto expected=ai::ComputeLimits(occupied).cacheBytes;
    Wait([&]{return manager.Status().limits.cacheBytes==expected;},"active watchdog retained the old parent cache quota",3500ms);
    Require(!manager.TryGet(L"0",2) && manager.TryGet(L"8",2) && !fake->Exited(2) && !manager.Status().paused,
        "shrinking parent cache did not evict old pixels while allowing the small active batch");
    fake->Release(2);Idle(manager,5000ms);
}

void ApplyCancelsRetainsCacheAndRejectsLate(const std::filesystem::path& root) {
    auto fake = std::make_shared<BatchFake>(); fake->automatic = false; fake->ignoreCancellation = true;
    auto samples = std::make_shared<Samples>();
    std::vector<std::shared_ptr<ImageData>> sources; auto pages = Document(root / L"sources", 5, sources);
    ai::Manager manager(nullptr, {}, root, {}, samples->Probe(), fake->Executor());
    manager.SetDocument(pages); manager.SetPriority({{L"0", sources[0]}}, 2);
    Wait([&] { return fake->Count() == 1; }, "apply fixture never started");
    fake->Release(0); Wait([&] { return fake->Count() == 2; }, "apply fixture never started background batch");
    auto cached = manager.TryGet(L"0", 2); Require(cached != nullptr, "apply fixture lost completed cache");
    ai::UserLimits user; user.manual = true; user.ramBytes = 16 * GiB; user.cpuPercent = 37; user.batchPages = 2;
    const auto started = std::chrono::steady_clock::now(); manager.SetUserLimits(user);
    Require(std::chrono::steady_clock::now() - started < 100ms && manager.TryGet(L"0", 2) == cached,
        "applying settings blocked or discarded completed display pixels");
    fake->Release(1); Wait([&] { return fake->Count() == 3; }, "apply did not replan cancelled pages");
    Require(!manager.TryGet(L"1", 2) && fake->At(2)->options.cpuPercent == 37 &&
        fake->At(2)->options.jobMemoryBytes == 8 * GiB && fake->At(2)->keys.size() == 2,
        "late old-settings output published or replacement ignored new limits");
    fake->Release(2); Wait([&] { return fake->Count() == 4; }, "new batch limit did not split remaining pages");
    fake->Release(3); Idle(manager);
    Require(manager.TryGet(L"0", 2) == cached && fake->At(3)->keys.size() == 2,
        "replan recomputed retained cache or lost bounded remaining batch");
}

void ExplicitSettingsReachLegacyRequests(const std::filesystem::path& root) {
    auto fake = std::make_shared<Fake>(); fake->automatic = true;
    auto samples = std::make_shared<Samples>(); auto source = Image(L"explicit-legacy");
    ai::Manager manager(nullptr, {}, root, fake->Executor(), samples->Probe());
    ai::UserLimits user; user.manual = true; user.cpuPercent = 19; manager.SetUserLimits(user);
    manager.SetRequests({{L"page", source}}, 2); Idle(manager);
    Require(fake->Count() == 1 && fake->At(0)->cpuPercent == 19,
        "explicit limits did not reach direct requests");
    user.error = L"corrupt saved manual limits"; manager.SetUserLimits(user);
    auto other = Image(L"invalid-legacy"); manager.SetRequests({{L"other", other}}, 2); Idle(manager);
    Require(fake->Count() == 1 && !manager.ErrorFor(L"other", 2).empty(),
        "direct requests bypassed corrupt saved settings");
}

void ApplyDuringPrepareAndStaleProbe(const std::filesystem::path& root) {
    auto fake = std::make_shared<BatchFake>(); auto samples = std::make_shared<Samples>();
    std::vector<std::shared_ptr<ImageData>> sources; auto pages = Document(root / L"sources", 1, sources); sources.clear();
    auto entered = std::make_shared<std::atomic_bool>(false), release = std::make_shared<std::atomic_bool>(false);
    pages[0].prepare = [entered, release](const std::wstring&, std::wstring&) {
        entered->store(true); while (!release->load()) std::this_thread::sleep_for(2ms); return true;
    };
    ai::Manager manager(nullptr, {}, root, {}, samples->Probe(), fake->Executor());
    manager.SetDocument(pages); manager.SetPriority({{L"0", {}}}, 2);
    Wait([&] { return entered->load(); }, "apply prepare fixture never prepared");
    ai::UserLimits user; user.manual = true; user.cpuPercent = 19;
    const auto started = std::chrono::steady_clock::now(); manager.SetUserLimits(user);
    Require(std::chrono::steady_clock::now() - started < 100ms, "applying settings waited for source preparation");
    release->store(true); Idle(manager);
    Require(fake->Count() == 1 && fake->At(0)->options.cpuPercent == 19,
        "old-settings preparation launched a child");
}

void LowerQuotaEvictsVisibleArtifactsWithoutSpin(const std::filesystem::path& root) {
    auto fake = std::make_shared<BatchFake>(); fake->paddedBytes = 600ULL * 1024 * 1024;
    auto samples = std::make_shared<Samples>();
    std::vector<std::shared_ptr<ImageData>> sources; auto pages = Document(root / L"sources", 3, sources);
    ai::Manager manager(nullptr, {}, root, {}, samples->Probe(), fake->Executor());
    manager.SetDocument(pages); manager.SetPriority({{L"0", sources[0]}, {L"1", sources[1]}, {L"2", sources[2]}}, 2); Idle(manager);
    auto displayed = manager.TryGet(L"0", 2);
    const auto artifact = root / L"batch-1-0" / L"result.png";
    Require(displayed && std::filesystem::exists(artifact), "quota fixture missing completed visible artifact");
    ai::UserLimits user; user.manual = true; user.diskBytes = GiB; manager.SetUserLimits(user);
    Wait([&] { return !std::filesystem::exists(artifact); }, "lower quota exempted a visible disk artifact");
    Require(manager.TryGet(L"0", 2) == displayed, "disk eviction discarded held display pixels");
    const auto count = fake->Count();
    for (int index = 0; index < 12; ++index) manager.SetPriority({{L"0", sources[0]}, {L"1", sources[1]}, {L"2", sources[2]}}, 2);
    std::this_thread::sleep_for(60ms);
    Require(fake->Count() == count && !manager.Pending(), "lower quota entered a stationary inference loop");
}

void RaiseQuotaResumesDeferredPagesWithoutNavigation(const std::filesystem::path& root) {
    auto fake = std::make_shared<BatchFake>(); fake->paddedBytes = GiB;
    auto samples = std::make_shared<Samples>();
    std::vector<std::shared_ptr<ImageData>> sources; auto pages = Document(root / L"sources", 3, sources);
    ai::UserLimits user; user.manual = true; user.diskBytes = GiB;
    ai::Manager manager(nullptr, {}, root, {}, samples->Probe(), fake->Executor());
    manager.SetUserLimits(user); manager.SetDocument(pages); manager.SetPriority({{L"0", sources[0]}}, 2);
    Wait([&] { return manager.Status().paused && manager.Status().complete == 1; }, "manual cache did not defer unfinished background pages");
    auto completed = manager.TryGet(L"0", 2);
    Require(completed && fake->Count() == 1 && !manager.TryGet(L"1", 2) && !manager.TryGet(L"2", 2) &&
        manager.ErrorFor(L"1", 2).empty() && manager.ErrorFor(L"2", 2).empty(),
        "quota fixture did not distinguish a completed page from deferred pages");
    for (int frame = 0; frame < 12; ++frame) manager.SetPriority({{L"0", sources[0]}}, 2);
    std::this_thread::sleep_for(60ms);
    Require(fake->Count() == 1 && !manager.Pending(), "quota-deferred pages spun before settings changed");
    user.diskBytes = 4 * GiB; manager.SetUserLimits(user);
    Wait([&] { return manager.Status().complete == 3; }, "increased quota did not resume deferred pages without navigation");
    Idle(manager);
    Require(fake->Count() == 2 && fake->At(0)->keys == std::vector<std::wstring>{L"0"} &&
        fake->At(1)->keys == std::vector<std::wstring>{L"1", L"2"} && manager.TryGet(L"0", 2) == completed,
        "quota replan recomputed a completed page or lost deferred page ordering");
}

void ManualGpuThresholdWatchdogAndRecovery(const std::filesystem::path& root) {
    auto fake = std::make_shared<BatchFake>(); fake->automatic = false;
    auto samples = std::make_shared<Samples>();
    std::vector<std::shared_ptr<ImageData>> sources; auto pages = Document(root / L"sources", 1, sources);
    ai::UserLimits user; user.manual = true; user.gpuReserveBytes = 8 * GiB;
    ai::Manager manager(nullptr, {}, root, {}, samples->Probe(), fake->Executor()); manager.SetUserLimits(user);
    manager.SetDocument(pages); manager.SetPriority({{L"0", sources[0]}}, 2);
    Wait([&] { return fake->Count() == 1; }, "manual GPU healthy threshold never admitted");
    auto unknown = Healthy(); unknown.gpuKnown = false; samples->Set(unknown);
    Wait([&] { return manager.Status().paused && fake->Exited(0); }, "manual GPU loss did not stop watchdog", 3500ms);
    std::this_thread::sleep_for(1100ms); Require(fake->Count() == 1, "unknown manual GPU recovered");
    samples->Set(Healthy());
    std::this_thread::sleep_for(1100ms); Require(fake->Count() == 1, "manual GPU recovered before three healthy samples");
    Wait([&] { return fake->Count() == 2; }, "manual GPU did not recover after three healthy samples", 4500ms);
    fake->Release(1); Idle(manager);
}
}

int wmain(const int argc, wchar_t** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    const auto root = argc > 1 ? std::filesystem::path(argv[1]) :
        std::filesystem::temp_directory_path() / L"EinzigAiManagerProbe";
    std::filesystem::create_directories(root);
    const std::pair<const char*, std::function<void(const std::filesystem::path&)>> tests[] = {
        {"inert-construction", InertConstruction},
        {"desired-order-duplicates", DesiredOrderAndDuplicates},
        {"cancel-preserves-cache-rejects-late", CancelAndCache},
        {"content-scale-versioning", ContentAndScale},
        {"weak-queued-sources", QueuedSourcesAreWeak},
        {"aliased-source-versioning", AliasedSourcesAreDifferentVersions},
        {"errors-do-not-spin", ErrorsDoNotSpin},
        {"ram-lru-disk-reload", MemoryLruAndDiskReload},
        {"disk-lru", DiskLru},
        {"stationary-viewport-budget", StationaryViewportDoesNotReload},
        {"clear-cache-late-result", ClearDropsCacheAndLateResult},
        {"nonblocking-destruction-no-notification", DestructionIsNonblocking},
        {"document-lazy-order-batches", LazyDocumentOrderAndBatches},
        {"document-progressive-priority-jump", ProgressiveJumpAndPartialSuccess},
        {"document-viewport-page-boundary-yield", ViewportYieldsAfterCompletedPage},
        {"document-viewport-withdraw-yield", ViewportReturnWithdrawsYield},
        {"document-viewport-prepare-preemption", ViewportCancelsDuringSourcePreparation},
        {"document-stable-content-identity", StableDocumentIdentity},
        {"document-errors-reactivation", DocumentErrorsRequireReactivation},
        {"document-pressure-three-samples", DocumentPressureAndRecovery},
        {"document-resource-retry-bound", DocumentResourceRetryBound},
        {"document-lazy-source-teardown", LazyPreparationAndNonblockingTeardown},
        {"document-snapshot-content-replacement", UpdatedDocumentSnapshotInvalidatesContent},
        {"document-rotation-preset", DocumentRotationAndPreset},
        {"document-quota-idle-current-leases", DocumentQuotaPausesWithoutSpin},
        {"document-output-bound-unknown-gpu", DocumentOutputBoundAndUnknownGpu},
        {"document-watchdog-actual-requirements", RunningBatchDoesNotSpendTheoreticalMaximum},
        {"document-fresh-preflight-batch-shrink", FreshPreflightShrinksPreparedBatch},
        {"document-prepare-pressure-before-decode", PreparePressureStopsBeforeDecode},
        {"document-fresh-ram-retained-sources", FreshRamBudgetBoundsRetainedSources},
        {"document-lazy-large-page-deferred", LazyPageThatFitsAloneIsDeferred},
        {"document-live-cache-shrink", LiveBudgetShrinksCompletedCache},
        {"settings-apply-cancel-cache-late", ApplyCancelsRetainsCacheAndRejectsLate},
        {"settings-direct-request-limits", ExplicitSettingsReachLegacyRequests},
        {"settings-apply-during-prepare", ApplyDuringPrepareAndStaleProbe},
        {"settings-lower-visible-disk-quota", LowerQuotaEvictsVisibleArtifactsWithoutSpin},
        {"settings-raise-quota-resumes-deferred", RaiseQuotaResumesDeferredPagesWithoutNavigation},
        {"settings-manual-gpu-watchdog-recovery", ManualGpuThresholdWatchdogAndRecovery},
    };
    int failures = 0;
    for (const auto& [name, test] : tests) {
        if (argc > 2 && std::filesystem::path(argv[2]).string() != name) continue;
        try { test(root / name); std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& error) { ++failures; std::cerr << "FAIL " << name << ": " << error.what() << '\n'; }
    }
    std::cout << "AI_MANAGER_PROBE " << (failures == 0 ? "PASS" : "FAIL") << " failures=" << failures << '\n';
    return failures == 0 ? 0 : 1;
}
