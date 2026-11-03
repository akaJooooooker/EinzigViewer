#include "AiManager.h"
#include "WicDecoder.h"
#include "ImageFingerprint.h"
#include "ImageRotation.h"

#include <objbase.h>

#include <algorithm>
#include <condition_variable>
#include <chrono>
#include <cstdint>
#include <map>
#include <mutex>
#include <set>
#include <thread>
#include <tuple>
#include <cwctype>

namespace ai {
namespace {
constexpr std::uint64_t MemoryBudget = 256ULL * 1024 * 1024;
constexpr std::uint64_t DiskBudget = 512ULL * 1024 * 1024;

bool SameOwner(const std::weak_ptr<ImageData>& left, const std::shared_ptr<ImageData>& right) {
    return left.lock().get()==right.get() && !left.owner_before(right) && !right.owner_before(left);
}

bool ValidSource(const ImageData& source, const int scale, std::wstring& error) {
    if (scale != 2 && scale != 4) { error = L"AI 僅支援 2× 或 4×。"; return false; }
    const std::uint64_t width = static_cast<std::uint64_t>(source.width) * scale;
    const std::uint64_t height = static_cast<std::uint64_t>(source.height) * scale;
    if (width == 0 || height == 0 || width > 16384 || height > 16384 ||
        width * height * 4 > MemoryBudget) {
        error = L"AI 輸出超過 16,384 像素或 256 MiB 上限，請改用較低倍率。";
        return false;
    }
    const auto rowBytes = static_cast<std::uint64_t>(source.width) * 4;
    if (source.stride < rowBytes || static_cast<std::uint64_t>(source.stride) * source.height > source.pixels.size()) {
        error = L"AI 原始圖片資料不完整。";
        return false;
    }
    return true;
}

bool ValidResult(const ImageData& image, const ImageData& source, const int scale) {
    return image.width == static_cast<std::uint64_t>(source.width) * scale &&
        image.height == static_cast<std::uint64_t>(source.height) * scale &&
        image.stride >= static_cast<std::uint64_t>(image.width) * 4 &&
        image.pixels.size() >= static_cast<std::uint64_t>(image.stride) * image.height &&
        image.pixels.size() <= MemoryBudget;
}

bool ResourceFailure(std::wstring error) {
    std::transform(error.begin(), error.end(), error.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    for (const auto* word : {L"memory", L"resource", L"gpu", L"device", L"oom", L"記憶體", L"資源", L"顯存"})
        if (error.find(word) != std::wstring::npos) return true;
    return false;
}
}

struct Manager::State : std::enable_shared_from_this<Manager::State> {
    struct Key {
        std::wstring page;
        std::uint64_t version = 0;
        int scale = 2;
        bool operator==(const Key&) const = default;
        bool operator<(const Key& other) const {
            return std::tie(page, version, scale) < std::tie(other.page, other.version, other.scale);
        }
    };
    struct Version {
        std::weak_ptr<ImageData> source;
        std::uint64_t number = 0;
    };
    struct Desired {
        Key key;
        std::weak_ptr<ImageData> source;
        // An evicted visible page is loaded again only when requested by the UI.
        // Otherwise a viewport larger than the RAM budget would decode forever.
        bool serviced = false;
    };
    struct Entry {
        std::shared_ptr<ImageData> image;
        std::weak_ptr<ImageData> displayed;
        Artifact artifact;
        std::uint64_t diskBytes = 0;
        std::uint64_t memoryUsed = 0;
        std::uint64_t diskUsed = 0;
        std::wstring error;
    };
    using Cache = std::map<Key, Entry>;
    struct Job {
        Key key;
        UserLimits userLimits;
        std::shared_ptr<std::atomic_bool> cancelled = std::make_shared<std::atomic_bool>(false);
    };

    mutable std::mutex mutex;
    std::condition_variable changed;
    HWND window = nullptr;
    RuntimeOptions options;
    std::filesystem::path root;
    Executor executor;
    bool started = false;
    bool stopping = false;
    int scale = 2;
    std::uint64_t nextVersion = 0;
    std::uint64_t clock = 0;
    std::uint64_t memoryBytes = 0;
    std::uint64_t diskBytes = 0;
    std::map<std::wstring, Version> versions;
    std::vector<Desired> desired;
    Cache cache;
    std::vector<Cache> retired;
    std::shared_ptr<Job> active;

    struct DocumentKey {
        std::wstring page;
        std::string fingerprint;
        int rotation = 0, scale = 2;
        unsigned tile = 128;
        bool operator<(const DocumentKey& other) const {
            return std::tie(page, fingerprint, rotation, scale, tile) <
                std::tie(other.page, other.fingerprint, other.rotation, other.scale, other.tile);
        }
    };
    struct Attempt {
        bool serviced = false, retryUsed = false, quotaDeferred = false;
        unsigned tile = 128;
        std::wstring error;
    };
    struct DocumentPage {
        Page page;
        std::string fingerprint;
        std::uint64_t revision = 0;
        bool verify = true;
        std::map<int, Attempt> attempts;
    };
    struct DocumentJob {
        std::shared_ptr<std::atomic_bool> cancelled = std::make_shared<std::atomic_bool>(false);
        std::uint64_t epoch = 0;
        int scale = 2;
        std::vector<std::wstring> pages;
        bool resourceFailure = false;
        bool running = false, yieldRequested = false;
        std::wstring reason;
        ResourceLimits limits;
        UserLimits userLimits;
    };
    using DocumentCache = std::map<DocumentKey, Entry>;
    ResourceProbe resources;
    BatchExecutor batchExecutor;
    bool documentMode = false, enabled = false, resourcePaused = false, quotaPaused = false;
    bool urgent = false, forceSingle = false;
    unsigned healthySamples = 0;
    std::uint64_t epoch = 0, documentMemory = 0, documentDisk = 0;
    std::vector<std::wstring> documentOrder, visible;
    std::map<std::wstring, DocumentPage> pages;
    DocumentCache documentCache;
    std::vector<DocumentCache> retiredDocuments;
    std::vector<Page> retiredPages;
    std::shared_ptr<DocumentJob> documentActive;
    ResourceLimits limits;
    UserLimits userLimits;
    ResourceSnapshot lastSample;
    bool limitsPending = false, haveSample = false;
    std::wstring pauseReason;
    std::chrono::steady_clock::time_point nextResourceSample{};

    DocumentKey CacheKey(const DocumentPage& page) const {
        const auto attempt = page.attempts.find(scale);
        return {page.page.key, page.fingerprint, page.page.rotation, scale,
            attempt == page.attempts.end() ? options.tileSize : attempt->second.tile};
    }
    bool IsVisible(const std::wstring& key) const {
        return std::find(visible.begin(), visible.end(), key) != visible.end();
    }
    bool Reusable(const Entry& entry) const {
        return entry.image || !entry.displayed.expired() || entry.artifact.lease;
    }
    bool Needs(const DocumentPage& page) const {
        if (page.verify) return true;
        const auto attempt = page.attempts.find(scale);
        return attempt == page.attempts.end() || !attempt->second.serviced;
    }
    bool DocumentWork() const {
        if (!documentMode) return false;
        if (limitsPending && (enabled || haveSample)) return true;
        if (!enabled) return false;
        if (resourcePaused) return true;
        if (quotaPaused) {
            if (visible.empty()) return false;
            const auto current = pages.find(visible.front());
            return current != pages.end() && Needs(current->second);
        }
        return std::any_of(pages.begin(), pages.end(), [&](const auto& page) { return Needs(page.second); });
    }
    std::vector<std::wstring> OrderedPages() const {
        std::vector<std::wstring> result;
        std::set<std::wstring> seen;
        const auto append = [&](const std::wstring& key) {
            if (pages.contains(key) && seen.insert(key).second) result.push_back(key);
        };
        for (const auto& key : visible) append(key);
        auto current = visible.empty() ? documentOrder.begin() :
            std::find(documentOrder.begin(), documentOrder.end(), visible.front());
        if (current == documentOrder.end()) current = documentOrder.begin();
        for (auto it = current; it != documentOrder.end(); ++it) append(*it);
        while (current != documentOrder.begin()) append(*--current);
        return result;
    }
    void Pause(const std::wstring& reason) {
        resourcePaused = true; quotaPaused = false; pauseReason = reason;
        healthySamples = 0;
        nextResourceSample = std::chrono::steady_clock::now() + std::chrono::seconds(1);
        Notify();
    }
    void EvictDocument(std::vector<std::shared_ptr<ImageData>>& images,
        std::vector<Artifact>& artifacts, const std::uint64_t reserveDisk = 0, const bool urgentDisk = false,
        const bool evictVisible = false) {
        while (documentMemory > limits.cacheBytes) {
            auto oldest = documentCache.end();
            for (auto it = documentCache.begin(); it != documentCache.end(); ++it)
                if (it->second.image && (oldest == documentCache.end() || it->second.memoryUsed < oldest->second.memoryUsed)) oldest = it;
            if (oldest == documentCache.end()) break;
            documentMemory -= oldest->second.image->ByteSize(); images.push_back(std::move(oldest->second.image));
        }
        while (documentDisk > limits.diskBytes || reserveDisk > limits.diskBytes - std::min(documentDisk, limits.diskBytes)) {
            auto oldest = documentCache.end();
            for (auto it = documentCache.begin(); it != documentCache.end(); ++it) {
                if (!it->second.artifact.lease || (!evictVisible && IsVisible(it->first.page))) continue;
                if (!urgentDisk && reserveDisk) continue;
                if (oldest == documentCache.end() || it->second.diskUsed < oldest->second.diskUsed) oldest = it;
            }
            if (oldest == documentCache.end()) break;
            documentDisk -= oldest->second.diskBytes; oldest->second.diskBytes = 0;
            artifacts.push_back(std::move(oldest->second.artifact));
            // Keep the serviced tombstone: a quota eviction cannot immediately
            // enqueue inference again while the reader remains stationary.
        }
    }

    void WorkDocument();

    auto FindDesired(const Key& key) {
        return std::find_if(desired.begin(), desired.end(), [&](const Desired& item) { return item.key == key; });
    }

    bool HasWork() const {
        return DocumentWork() || std::any_of(desired.begin(), desired.end(), [](const Desired& item) {
            return !item.serviced && !item.source.expired();
        });
    }

    void Notify() const {
        // Called with mutex held: destruction clears window under the same lock.
        // No payload owns a raw Manager, App, State or image pointer.
        if (!stopping && window) PostMessageW(window, Manager::ReadyMessage, 0, 0);
    }

    void Evict(std::vector<std::shared_ptr<ImageData>>& images, std::vector<Artifact>& artifacts) {
        while (memoryBytes > MemoryBudget) {
            auto oldest = cache.end();
            for (auto entry = cache.begin(); entry != cache.end(); ++entry) {
                if (entry->second.image && (oldest == cache.end() ||
                    entry->second.memoryUsed < oldest->second.memoryUsed)) oldest = entry;
            }
            if (oldest == cache.end()) break;
            memoryBytes -= oldest->second.image->ByteSize();
            images.push_back(std::move(oldest->second.image));
        }
        while (diskBytes > DiskBudget) {
            auto oldest = cache.end();
            for (auto entry = cache.begin(); entry != cache.end(); ++entry) {
                if (entry->second.artifact.lease && (oldest == cache.end() ||
                    entry->second.diskUsed < oldest->second.diskUsed)) oldest = entry;
            }
            if (oldest == cache.end()) break;
            diskBytes -= oldest->second.diskBytes;
            oldest->second.diskBytes = 0;
            artifacts.push_back(std::move(oldest->second.artifact));
        }
        // Cache indices with neither pixels, a file nor an error need no lifetime.
        std::erase_if(cache, [](const auto& value) {
            const auto& entry = value.second;
            return !entry.image && entry.displayed.expired() && !entry.artifact.lease && entry.error.empty();
        });
    }

    void Work() {
        const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        struct ComScope {
            HRESULT result;
            ~ComScope() { if (SUCCEEDED(result)) CoUninitialize(); }
        } comScope{comResult};
        for (;;) {
            std::vector<Cache> discarded;
            std::vector<DocumentCache> discardedDocuments;
            std::vector<Page> discardedPages;
            std::shared_ptr<Job> job;
            std::shared_ptr<ImageData> source;
            Artifact disk;
            {
                std::unique_lock lock(mutex);
                changed.wait(lock, [&] { return stopping || !retired.empty() || !retiredDocuments.empty() || !retiredPages.empty() || HasWork(); });
                discarded.swap(retired);
                discardedDocuments.swap(retiredDocuments); discardedPages.swap(retiredPages);
                if (stopping) break;
                if (documentMode) {
                    lock.unlock(); discarded.clear(); discardedDocuments.clear(); discardedPages.clear();
                    try { WorkDocument(); }
                    catch (...) {
                        std::lock_guard failureLock(mutex);
                        if (documentActive) {
                            documentActive->cancelled->store(true);
                            for (const auto& key : documentActive->pages) {
                                const auto page = pages.find(key); if (page == pages.end()) continue;
                                auto& attempt = page->second.attempts[scale]; attempt.serviced = true;
                                attempt.error = L"AI 背景記憶體或檔案處理失敗。 / AI background memory or file operation failed.";
                                page->second.verify = false;
                            }
                            documentActive.reset(); Notify();
                        }
                    }
                    continue;
                }
                for (auto& item : desired) {
                    if (item.serviced) continue;
                    source = item.source.lock();
                    if (!source) { item.serviced = true; continue; }
                    auto entry = cache.find(item.key);
                    if (entry != cache.end() && (entry->second.image ||
                        !entry->second.displayed.expired() || !entry->second.error.empty())) {
                        item.serviced = true;
                        source.reset();
                        continue;
                    }
                    job = std::make_shared<Job>();
                    job->key = item.key;
                    job->userLimits = userLimits;
                    active = job;
                    if (entry != cache.end()) disk = entry->second.artifact;
                    break;
                }
            }
            // Leases can delete files; large pixel buffers can deallocate. Both
            // happen outside the UI-facing lock, exclusively on this worker.
            discarded.clear();
            if (!job) continue;

            Output output;
            std::wstring error;
            try {
                if (ValidSource(*source, job->key.scale, error) && !job->cancelled->load()) {
                    RuntimeOptions runtime = options;
                    runtime.cpuPercent = job->userLimits.manual ? job->userLimits.cpuPercent : 100;
                    bool admitted = ValidateUserLimits(job->userLimits, error);
                    if (admitted && job->userLimits.manual) {
                        ResourceSnapshot current;
                        try { current = resources(root, options.gpu); } catch (...) {}
                        const auto bytes = static_cast<std::uint64_t>(source->width) * source->height * 4 * job->key.scale * job->key.scale;
                        const auto bounded = ComputeLimits(current, bytes, job->userLimits);
                        admitted = bounded.allowed && bytes <= bounded.outputBytes && source->ByteSize() <= bounded.transientBytes / 3;
                        if (!admitted) error = bounded.reason.empty() ? L"AI 來源超過資源配額。 / AI source exceeds the resource budget." : bounded.reason;
                        runtime.maximumOutputBytes = std::min(MemoryBudget, bounded.outputBytes);
                        runtime.maximumBatchBytes = std::min(MemoryBudget, bounded.workBytes);
                        runtime.jobMemoryBytes = bounded.jobBytes; runtime.stagingBytes = bounded.stagingBytes;
                    }
                    if (!admitted) {
                        output = {};
                    } else
                    if (disk.lease && !disk.png.empty()) {
                        output.artifact = disk;
                        output.image = WicDecoder::Decode(disk.png.wstring(), error, MemoryBudget);
                        if (output.image) output.image->path = source->path;
                    } else {
                        output = executor(runtime, *source, job->key.scale, root, *job->cancelled, error);
                    }
                    if (output.image && !ValidResult(*output.image, *source, job->key.scale)) {
                        output.image.reset();
                        error = L"AI 輸出尺寸或圖片資料不符。";
                    }
                    if (!output.image && error.empty() && !job->cancelled->load()) error = L"AI 未產生有效的圖片。";
                }
            } catch (const std::exception&) {
                error = L"AI 背景處理失敗。";
                output.image.reset();
            } catch (...) {
                error = L"AI 背景處理失敗。";
                output.image.reset();
            }

            std::uint64_t artifactBytes = 0;
            if (output.image && output.artifact.lease && !output.artifact.png.empty()) {
                std::error_code filesystemError;
                const auto bytes = std::filesystem::file_size(output.artifact.png, filesystemError);
                if (!filesystemError) artifactBytes = bytes;
            }
            std::vector<std::shared_ptr<ImageData>> discardedImages;
            std::vector<Artifact> discardedArtifacts;
            {
                std::lock_guard lock(mutex);
                auto item = FindDesired(job->key);
                if (!stopping && !job->cancelled->load() && item != desired.end()) {
                    item->serviced = true;
                    auto& entry = cache[job->key];
                    if (entry.image) memoryBytes -= entry.image->ByteSize();
                    if (entry.artifact.lease) diskBytes -= entry.diskBytes;
                    discardedImages.push_back(std::move(entry.image));
                    discardedArtifacts.push_back(std::move(entry.artifact));
                    entry.error = output.image ? std::wstring{} : error;
                    entry.image = std::move(output.image);
                    entry.displayed = entry.image;
                    entry.memoryUsed = entry.diskUsed = ++clock;
                    entry.diskBytes = 0;
                    if (entry.image) {
                        memoryBytes += entry.image->ByteSize();
                        if (artifactBytes > 0 && artifactBytes <= DiskBudget) {
                            entry.artifact = std::move(output.artifact);
                            entry.diskBytes = artifactBytes;
                            diskBytes += artifactBytes;
                        }
                    }
                    Evict(discardedImages, discardedArtifacts);
                    active.reset();
                    Notify();
                } else {
                    active.reset();
                }
            }
            // source/output/disk and evicted values are released on this thread.
        }
    }
};

void Manager::State::WorkDocument() {
    std::shared_ptr<DocumentJob> job;
    ResourceSnapshot sample;
    UserLimits configuration;
    std::uint64_t sampledEpoch = 0;
    bool probe = false;
    {
        std::unique_lock lock(mutex);
        if (stopping || !documentMode || (!enabled && !limitsPending)) return;
        sampledEpoch = epoch;
        if (resourcePaused) {
            const auto due = nextResourceSample;
            changed.wait_until(lock, due, [&] { return stopping || !enabled || !documentMode || epoch != sampledEpoch; });
            if (stopping || !enabled || !documentMode) return;
        }
        sampledEpoch = epoch; configuration = userLimits; probe = enabled;
        if (!probe && !haveSample) { limitsPending = false; return; }
        sample = lastSample;
    }
    if (probe) {
        try { sample = resources(root, options.gpu); }
        catch (...) { sample = {}; }
    }
    const auto proposed = ComputeLimits(sample, 0, configuration);
    std::vector<std::shared_ptr<ImageData>> discardedImages;
    std::vector<Artifact> discardedArtifacts;
    {
        std::lock_guard lock(mutex);
        if (stopping || !documentMode || sampledEpoch != epoch) return;
        const bool enforceQuota = limitsPending || proposed.diskBytes < limits.diskBytes;
        lastSample = sample; haveSample = true; limitsPending = false;
        limits = proposed;
        EvictDocument(discardedImages, discardedArtifacts, 0, false, enforceQuota);
        if (!enabled) return;
        if (resourcePaused) {
            if (ResourcesRecovered(sample, configuration) && proposed.allowed) ++healthySamples;
            else healthySamples = 0;
            nextResourceSample = std::chrono::steady_clock::now() + std::chrono::seconds(1);
            if (healthySamples < 3) return;
            resourcePaused = false; pauseReason.clear(); forceSingle = true;
        }
        std::wstring reason;
        if (!proposed.allowed || ResourcePressure(sample, reason, configuration)) {
            Pause(reason.empty() ? proposed.reason : reason); return;
        }
        job = std::make_shared<DocumentJob>(); job->epoch = epoch; job->scale = scale; job->limits = proposed;
        job->userLimits = configuration;
        const auto order = OrderedPages();
        for (const auto& key : order) {
            auto& page = pages.at(key);
            if (!Needs(page)) continue;
            auto& attempt = page.attempts[scale];
            attempt.quotaDeferred = false;
            if (!attempt.retryUsed) attempt.tile = options.tileSize;
            if (!job->pages.empty() && (urgent || forceSingle || attempt.tile == 64 ||
                page.attempts[scale].tile != pages.at(job->pages.front()).attempts[scale].tile)) break;
            job->pages.push_back(key);
            if (urgent || forceSingle || attempt.tile == 64 || job->pages.size() >= std::min(8U, proposed.batchPages)) break;
        }
        if (job->pages.empty()) return;
        documentActive = job; urgent = false; forceSingle = false;
        Notify();
    }
    discardedImages.clear(); discardedArtifacts.clear();

    struct Prepared {
        DocumentKey key;
        std::uint64_t revision = 0;
        std::shared_ptr<ImageData> image;
    };
    std::vector<Prepared> prepared;
    std::vector<BatchInput> inputs;
    std::uint64_t outputBytes = 0, transientBytes = 0;
    unsigned preparedPageLimit = std::min(8U, proposed.batchPages);
    const auto markFailure = [&](const std::wstring& key, const std::uint64_t revision, const std::wstring& error) {
        std::lock_guard lock(mutex);
        const auto found = pages.find(key);
        if (stopping || !enabled || job->epoch != epoch || job->cancelled->load() ||
            found == pages.end() || found->second.revision != revision) return;
        auto& attempt = found->second.attempts[job->scale]; attempt.serviced = true; attempt.error = error;
        found->second.verify = false;
        Notify();
    };
    for (const auto& key : job->pages) {
        Page page;
        std::uint64_t revision = 0;
        {
            std::lock_guard lock(mutex);
            if (stopping || !enabled || epoch != job->epoch || job->cancelled->load()) break;
            const auto found = pages.find(key); if (found == pages.end()) continue;
            page = found->second.page; revision = found->second.revision;
        }
        std::wstring error;
        std::shared_ptr<ImageData> source;
        std::string fingerprint;
        bool defer = false;
        std::uint64_t sourceTransientLimit = proposed.transientBytes;
        try {
            source = page.decoded.lock();
            const bool wasDecoded = source != nullptr;
            if (!source) {
                if (page.prepare && !page.prepare(page.path.wstring(), error)) {
                    if (error.empty()) error = L"AI 來源準備失敗。 / Could not prepare AI source.";
                } else if (!job->cancelled->load()) {
                    ResourceSnapshot freshSample;
                    try { freshSample = resources(root, options.gpu); } catch (...) {}
                    const auto freshLimits = ComputeLimits(freshSample, 0, job->userLimits);
                    if (!freshLimits.allowed) {
                        std::lock_guard lock(mutex);
                        if (enabled && epoch == job->epoch && !job->cancelled->load()) {
                            limits = freshLimits; Pause(freshLimits.reason); job->cancelled->store(true);
                        }
                    } else {
                        preparedPageLimit = std::min(preparedPageLimit, freshLimits.batchPages);
                        sourceTransientLimit = std::min(sourceTransientLimit, freshLimits.transientBytes);
                        const auto available = sourceTransientLimit - std::min(transientBytes, sourceTransientLimit);
                        if (!available) defer = true;
                        else {
                            source = WicDecoder::Decode(page.path.wstring(), error, std::min(512ULL * 1024 * 1024, available / 3));
                            // A page can fit by itself but exceed the remainder
                            // of this batch. Retry alone before declaring failure.
                            if(!source && !inputs.empty())defer=true;
                        }
                    }
                }
            }
            if (job->cancelled->load()) break;
            if (source) {
                const auto available = sourceTransientLimit - std::min(transientBytes, sourceTransientLimit);
                if (source->ByteSize() > available / 3) {
                    if (!inputs.empty()) { defer = true; source.reset(); }
                    else {
                    error = L"AI 來源超過暫存記憶體配額。 / AI source exceeds working memory quota.";
                    source.reset();
                    }
                }
            }
            if (source) {
                fingerprint = source->sourceFingerprint;
                if (fingerprint.empty()) {
                    if (wasDecoded && page.rotation % 4) {
                        auto base = RotateImage(*source, -page.rotation, error);
                        if (base) fingerprint = ComputeImageFingerprint(*base);
                        else source.reset();
                    } else fingerprint = ComputeImageFingerprint(*source);
                }
                if (source && !wasDecoded && page.rotation % 4) {
                    source = RotateImage(*source, page.rotation, error);
                    if (source) source->sourceFingerprint = fingerprint;
                }
            }
        } catch (...) { error = L"AI 來源解碼或指紋失敗。 / Could not decode or fingerprint AI source."; source.reset(); }
        if (job->cancelled->load()) break;
        if (defer) break;
        if (!source) { markFailure(key, revision, error.empty() ? L"AI 原始圖片無效。 / Invalid AI source." : error); continue; }
        const auto finalBytes = static_cast<std::uint64_t>(source->width) * source->height * 4 * job->scale * job->scale;
        const auto working = static_cast<std::uint64_t>(source->ByteSize()) * 3 + (job->scale == 8 ? finalBytes / 4 : 0);
        if (!ValidateOutputSize(*source, job->scale, error, proposed.outputBytes, std::min(16384U, options.maximumDimension)) ||
            working > proposed.transientBytes) {
            markFailure(key, revision, error.empty() ? L"AI 暫存記憶體不足，請降低倍率。 / AI working memory limit; use a lower scale." : error);
            continue;
        }
        if (!inputs.empty() && (finalBytes > proposed.workBytes - std::min(outputBytes, proposed.workBytes) ||
            working > proposed.transientBytes - std::min(transientBytes, proposed.transientBytes))) break;
        DocumentKey cacheKey;
        Artifact artifact;
        bool cached = false, reload = false;
        {
            std::lock_guard lock(mutex);
            const auto found = pages.find(key);
            if (stopping || !enabled || epoch != job->epoch || job->cancelled->load() ||
                found == pages.end() || found->second.revision != revision) continue;
            auto& descriptor = found->second;
            if (descriptor.fingerprint != fingerprint) {
                descriptor.fingerprint = fingerprint;
                descriptor.attempts.clear();
            }
            descriptor.verify = false;
            auto& attempt = descriptor.attempts[job->scale];
            if (!attempt.retryUsed) attempt.tile = options.tileSize;
            cacheKey = {key, fingerprint, page.rotation, job->scale, attempt.tile};
            const auto entry = documentCache.find(cacheKey);
            if (entry != documentCache.end() && Reusable(entry->second)) {
                reload = !entry->second.image && entry->second.displayed.expired() && IsVisible(key) && entry->second.artifact.lease;
                artifact = entry->second.artifact;
                attempt.serviced = true; attempt.error.clear(); cached = true;
                entry->second.diskUsed = ++clock;
            }
        }
        if (cached && !reload) { std::lock_guard lock(mutex); Notify(); continue; }
        if (reload) {
            Output output;
            output.artifact = artifact;
            output.image = WicDecoder::Decode(artifact.png.wstring(), error, proposed.outputBytes);
            if (output.image) { output.image->path = source->path; output.image->sourceFingerprint = fingerprint; }
            std::lock_guard lock(mutex);
            if (output.image && !job->cancelled->load() && enabled && epoch == job->epoch) {
                auto& entry = documentCache[cacheKey];
                if (entry.image) documentMemory -= entry.image->ByteSize();
                discardedImages.push_back(std::move(entry.image)); entry.image = std::move(output.image);
                entry.displayed = entry.image; entry.memoryUsed = entry.diskUsed = ++clock;
                documentMemory += entry.image->ByteSize(); EvictDocument(discardedImages, discardedArtifacts); Notify();
            }
            continue;
        }
        prepared.push_back({cacheKey, revision, source}); inputs.push_back({key, std::move(source)});
        outputBytes += finalBytes; transientBytes += working;
        if (inputs.size() >= preparedPageLimit) break;
    }
    if (job->cancelled->load() || inputs.empty()) {
        std::lock_guard lock(mutex); if (documentActive == job) documentActive.reset(); Notify(); return;
    }
    // Source preparation may include a bounded network wait. Probe again at
    // the allocation/process boundary instead of spending its old snapshot.
    try { sample = resources(root, options.gpu); } catch (...) { sample = {}; }
    const auto freshLimits = ComputeLimits(sample, 0, job->userLimits);
    if (freshLimits.allowed) {
        std::size_t retained = 0;
        outputBytes = transientBytes = 0;
        while (retained < prepared.size() && retained < std::min(8U, freshLimits.batchPages)) {
            const auto& image = *prepared[retained].image;
            std::wstring sizeError;
            const auto bytes = static_cast<std::uint64_t>(image.width) * image.height * 4 * job->scale * job->scale;
            const auto working = static_cast<std::uint64_t>(image.ByteSize()) * 3 + (job->scale == 8 ? bytes / 4 : 0);
            if (!ValidateOutputSize(image, job->scale, sizeError, freshLimits.outputBytes, std::min(16384U, options.maximumDimension)) ||
                working > freshLimits.transientBytes) {
                if (retained) break;
                markFailure(prepared.front().key.page, prepared.front().revision, sizeError.empty() ?
                    L"AI 新資源額度無法容納本頁，請降低倍率。 / Current AI resource budget cannot fit this page; use a lower scale." : sizeError);
                prepared.erase(prepared.begin()); inputs.erase(inputs.begin()); continue;
            }
            if (bytes > freshLimits.workBytes - std::min(outputBytes, freshLimits.workBytes) ||
                working > freshLimits.transientBytes - std::min(transientBytes, freshLimits.transientBytes)) break;
            outputBytes += bytes; transientBytes += working; ++retained;
        }
        prepared.resize(retained); inputs.resize(retained);
    }
    auto batchLimits = ComputeLimits(sample, outputBytes, job->userLimits);
    {
        std::lock_guard lock(mutex);
        if (job->cancelled->load() || !enabled || epoch != job->epoch) { documentActive.reset(); return; }
        if (inputs.empty() && batchLimits.allowed) { documentActive.reset(); Notify(); return; }
        if (!visible.empty() && Needs(pages.at(visible.front())) &&
            std::none_of(inputs.begin(), inputs.end(), [&](const auto& input) { return input.key == visible.front(); })) {
            urgent = true; job->cancelled->store(true); documentActive.reset(); return;
        }
        job->pages.clear(); for (const auto& input : inputs) job->pages.push_back(input.key);
        limits = batchLimits;
        const bool current = !visible.empty() && inputs.front().key == visible.front();
        EvictDocument(discardedImages, discardedArtifacts, outputBytes, current);
        if (!batchLimits.allowed) { Pause(batchLimits.reason); documentActive.reset(); return; }
        if (outputBytes > batchLimits.diskBytes - std::min(documentDisk, batchLimits.diskBytes)) {
            quotaPaused = true; pauseReason = L"AI 磁碟快取配額不足，背景預處理暫停。 / AI disk cache quota is full; background work paused.";
            // Deferred work stays idle in this viewport, but applying settings
            // must distinguish it from a completed result's eviction tombstone.
            for (const auto& input : inputs) {
                auto& attempt = pages.at(input.key).attempts[job->scale];
                attempt.serviced = true; attempt.quotaDeferred = true;
            }
            documentActive.reset(); Notify(); return;
        }
        quotaPaused = false; pauseReason.clear();
    }
    discardedImages.clear(); discardedArtifacts.clear();
    RuntimeOptions runtime = options;
    runtime.tileSize = prepared.front().key.tile;
    runtime.maximumOutputBytes = batchLimits.outputBytes; runtime.maximumBatchBytes = batchLimits.workBytes;
    runtime.jobMemoryBytes = batchLimits.jobBytes; runtime.stagingBytes = batchLimits.stagingBytes;
    runtime.cpuPercent = batchLimits.cpuPercent;
    runtime.maximumDimension = std::min(16384U, options.maximumDimension);
    auto nextWatch = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    const auto weak = weak_from_this();
    runtime.stopReason = [weak, job, nextWatch, outputBytes, transientBytes]() mutable -> std::wstring {
        if (job->cancelled->load()) return job->reason;
        const auto now = std::chrono::steady_clock::now();
        if (now < nextWatch) return {};
        nextWatch = now + std::chrono::seconds(1);
        const auto state = weak.lock(); if (!state) return L"AI 已取消。 / AI cancelled.";
        ResourceSnapshot current;
        try { current = state->resources(state->root, state->options.gpu); } catch (...) {}
        std::wstring reason; const auto currentLimits = ComputeLimits(current, 0, job->userLimits);
        const bool pressure = ResourcePressure(current, reason, job->userLimits);
        std::vector<std::shared_ptr<ImageData>> images;
        std::vector<Artifact> artifacts;
        std::unique_lock lock(state->mutex);
        if (state->stopping || !state->enabled || state->epoch != job->epoch) {
            job->cancelled->store(true); return L"AI 已取消。 / AI cancelled.";
        }
        const bool parentQuotaChanged=state->limits.cacheBytes!=currentLimits.cacheBytes ||
            state->limits.displayBytes!=currentLimits.displayBytes || state->limits.diskBytes!=currentLimits.diskBytes;
        const bool lowerDiskQuota = currentLimits.diskBytes < state->limits.diskBytes;
        state->limits=currentLimits;
        state->lastSample=current; state->haveSample=true;
        state->EvictDocument(images,artifacts,0,false,lowerDiskQuota);
        if(parentQuotaChanged)state->Notify();
        // A running child consumes RAM/VRAM itself. New theoretical maxima
        // consequently shrink even when this concrete batch still fits. Check
        // its actual retained sources/output instead of reserving the unused
        // original maximum a second time.
        if (pressure || !currentLimits.allowed || outputBytes > currentLimits.workBytes ||
            transientBytes > currentLimits.transientBytes) {
            if (reason.empty()) reason = currentLimits.reason.empty() ?
                L"AI 可用資源預算降低，暫停後重新檢查。 / AI resource budget decreased; pausing to recheck." : currentLimits.reason;
            job->resourceFailure = true; job->reason = reason; job->cancelled->store(true);
            state->Pause(reason);
            lock.unlock();return reason;
        }
        lock.unlock();
        return {};
    };
    std::vector<bool> completed(inputs.size(), false);
    const Completed publish = [&](const std::size_t index, Output output) {
        if (index >= prepared.size() || completed[index] || !output.image || job->cancelled->load()) return;
        const auto& input = prepared[index]; const auto& source = *input.image;
        if (output.image->width != static_cast<std::uint64_t>(source.width) * job->scale ||
            output.image->height != static_cast<std::uint64_t>(source.height) * job->scale ||
            output.image->stride < static_cast<std::uint64_t>(output.image->width) * 4 ||
            static_cast<std::uint64_t>(output.image->stride) * output.image->height > output.image->pixels.size() ||
            output.image->ByteSize() > batchLimits.outputBytes) return;
        output.image->path = source.path; output.image->sourceFingerprint = input.key.fingerprint;
        std::uint64_t bytes = 0;
        if (output.artifact.lease && !output.artifact.png.empty()) {
            std::error_code ignored; bytes = std::filesystem::file_size(output.artifact.png, ignored);
            if (ignored) bytes = 0;
        }
        std::vector<std::shared_ptr<ImageData>> images; std::vector<Artifact> artifacts;
        {
            std::lock_guard lock(mutex);
            const auto page = pages.find(input.key.page);
            if (stopping || !enabled || epoch != job->epoch || job->cancelled->load() ||
                page == pages.end() || page->second.revision != input.revision || page->second.fingerprint != input.key.fingerprint) return;
            auto& entry = documentCache[input.key];
            if (entry.image) documentMemory -= entry.image->ByteSize();
            if (entry.artifact.lease) documentDisk -= entry.diskBytes;
            images.push_back(std::move(entry.image)); artifacts.push_back(std::move(entry.artifact));
            entry.image = std::move(output.image); entry.displayed = entry.image; entry.error.clear();
            entry.diskBytes = 0; entry.memoryUsed = entry.diskUsed = ++clock;
            documentMemory += entry.image->ByteSize();
            if (bytes && bytes <= limits.diskBytes) {
                entry.artifact = std::move(output.artifact); entry.diskBytes = bytes; documentDisk += bytes;
            }
            auto& attempt = page->second.attempts[job->scale]; attempt.serviced = true; attempt.error.clear();
            completed[index] = true; EvictDocument(images, artifacts);
            // Keep the verified result before yielding the rest of this batch.
            if(job->yieldRequested)job->cancelled->store(true);
            Notify();
        }
    };
    std::wstring error;
    {
        std::lock_guard lock(mutex);
        if(stopping || !enabled || epoch!=job->epoch || job->cancelled->load()) {
            if(documentActive==job)documentActive.reset();Notify();return;
        }
        job->running=true;
    }
    try { batchExecutor(runtime, inputs, job->scale, root, *job->cancelled, publish, error); }
    catch (...) { error = L"AI 背景處理失敗。 / AI background processing failed."; }
    {
        std::lock_guard lock(mutex);
        if (!stopping && enabled && epoch == job->epoch) {
            bool retry = false;
            for (std::size_t i = 0; i < prepared.size(); ++i) {
                if (completed[i]) continue;
                const auto page = pages.find(prepared[i].key.page);
                if (page == pages.end() || page->second.revision != prepared[i].revision) continue;
                auto& attempt = page->second.attempts[job->scale];
                if ((job->resourceFailure || (!job->cancelled->load() && ResourceFailure(error))) &&
                    !attempt.retryUsed && attempt.tile != 64) {
                    attempt.retryUsed = true; attempt.tile = 64; attempt.serviced = false; retry = true;
                } else if (!job->cancelled->load() || job->resourceFailure) {
                    attempt.serviced = true; attempt.error = error.empty() ?
                        L"AI 未產生有效的圖片。 / AI produced no valid image." : error;
                }
            }
            if (retry) Pause(error.empty() ? job->reason : error);
        }
        if (documentActive == job) documentActive.reset(); Notify();
    }
}

Manager::Manager(const HWND notificationWindow, RuntimeOptions options,
    std::filesystem::path workRoot, Executor executor, ResourceProbe resources, BatchExecutor batchExecutor) : state_(std::make_shared<State>()) {
    state_->window = notificationWindow;
    state_->options = std::move(options);
    state_->root = std::move(workRoot);
    state_->executor = executor ? executor : Executor{Run};
    state_->resources = resources ? std::move(resources) : ResourceProbe{ProbeResources};
    if (batchExecutor) state_->batchExecutor = std::move(batchExecutor);
    else if (executor) state_->batchExecutor = [execute = std::move(executor)](const RuntimeOptions& runtime,
        const std::vector<BatchInput>& inputs, int scale, const std::filesystem::path& root,
        const std::atomic_bool& cancelled, const Completed& complete, std::wstring& error) {
        for (std::size_t i = 0; i < inputs.size() && !cancelled.load(); ++i) {
            auto output = execute(runtime, *inputs[i].image, scale, root, cancelled, error);
            if (output.image) complete(i, std::move(output));
            if (!error.empty()) break;
        }
    };
    else state_->batchExecutor = RunBatch;
}

void Manager::SetDocument(std::vector<Page> descriptors) {
    const auto state = state_; std::lock_guard lock(state->mutex);
    if (state->stopping) return;
    state->documentMode = true;
    state->desired.clear(); if (state->active) state->active->cancelled->store(true);
    std::map<std::wstring, State::DocumentPage> updated;
    std::vector<std::wstring> order;
    for (auto& descriptor : descriptors) {
        if (descriptor.key.empty() || updated.contains(descriptor.key)) continue;
        order.push_back(descriptor.key);
        auto existing = state->pages.find(descriptor.key);
        State::DocumentPage page;
        if (existing != state->pages.end()) page = std::move(existing->second);
        if (page.page.path != descriptor.path || page.page.rotation != descriptor.rotation) {
            page.fingerprint.clear(); page.attempts.clear(); page.verify = true; ++page.revision;
            if (state->documentActive) state->documentActive->cancelled->store(true);
        }
        if (const auto decoded = descriptor.decoded.lock(); decoded && !SameOwner(page.page.decoded, decoded) &&
            (decoded->sourceFingerprint.empty() || decoded->sourceFingerprint != page.fingerprint)) {
            page.verify = true; page.attempts.clear(); ++page.revision;
            if (state->documentActive) state->documentActive->cancelled->store(true);
        }
        if (page.page.lease || page.page.prepare) state->retiredPages.push_back(std::move(page.page));
        page.page = std::move(descriptor);
        updated.emplace(page.page.key, std::move(page));
    }
    for (auto& [key, page] : state->pages) if (!updated.contains(key)) state->retiredPages.push_back(std::move(page.page));
    state->pages = std::move(updated); state->documentOrder = std::move(order);
    state->changed.notify_one();
}

void Manager::SetPriority(std::vector<Request> requests, const int scale) {
    const auto state = state_; std::lock_guard lock(state->mutex);
    if (state->stopping || !state->documentMode) return;
    const bool reactivated = !state->enabled;
    const bool scaleChanged = state->scale != scale;
    const auto previous = state->visible;
    state->scale = scale; state->enabled = true;
    std::vector<std::wstring> visible;
    for (const auto& request : requests) {
        const auto found = state->pages.find(request.key);
        if (found == state->pages.end() || std::find(visible.begin(), visible.end(), request.key) != visible.end()) continue;
        visible.push_back(request.key);
        auto& page = found->second;
        if (request.source && !SameOwner(page.page.decoded, request.source)) {
            if (request.source->sourceFingerprint.empty() || request.source->sourceFingerprint != page.fingerprint) {
                page.verify = true; ++page.revision;
                page.attempts.clear();
                if (state->documentActive && std::find(state->documentActive->pages.begin(), state->documentActive->pages.end(), request.key) != state->documentActive->pages.end())
                    state->documentActive->cancelled->store(true);
            }
            page.page.decoded = request.source;
        }
    }
    state->visible = std::move(visible);
    const bool viewportChanged = previous != state->visible || scaleChanged || reactivated;
    if (reactivated) {
        state->resourcePaused = false; state->quotaPaused = false; state->pauseReason.clear(); state->healthySamples = 0;
        for (auto& [key, page] : state->pages) {
            auto& attempt = page.attempts[scale];
            if (!attempt.error.empty()) { attempt = {}; }
        }
    }
    if (viewportChanged) {
        for (const auto& key : state->visible) {
            auto& page = state->pages.at(key); auto& attempt = page.attempts[scale];
            const auto cached = state->documentCache.find(state->CacheKey(page));
            if (attempt.error.empty() && (cached == state->documentCache.end() ||
                (!cached->second.image && cached->second.displayed.expired()))) attempt.serviced = false;
        }
        if (!state->visible.empty() && state->Needs(state->pages.at(state->visible.front()))) state->urgent = true;
    }
    if(state->documentActive) {
        const bool different=!state->visible.empty() && state->Needs(state->pages.at(state->visible.front())) &&
            std::find(state->documentActive->pages.begin(),state->documentActive->pages.end(),state->visible.front())==state->documentActive->pages.end();
        if(scaleChanged || (different && !state->documentActive->running))state->documentActive->cancelled->store(true);
        else state->documentActive->yieldRequested=different;
    }
    if (!state->started && state->HasWork()) {
        std::thread([state] { state->Work(); }).detach(); state->started = true;
    }
    state->changed.notify_one();
}

void Manager::SetUserLimits(UserLimits user) {
    const auto state = state_; std::lock_guard lock(state->mutex);
    if (state->stopping) return;
    state->userLimits = std::move(user); ++state->epoch;
    if (state->active) state->active->cancelled->store(true);
    if (state->documentActive) state->documentActive->cancelled->store(true);
    state->limitsPending = true;
    state->resourcePaused = state->quotaPaused = false;
    state->pauseReason.clear(); state->healthySamples = 0;
    // Completed entries and quota-eviction tombstones stay serviced. Failed or
    // quota-deferred attempts need admission under the new configuration.
    for (auto& [key, page] : state->pages) {
        for (auto& [scale, attempt] : page.attempts)
            if (!attempt.error.empty() || attempt.quotaDeferred) attempt = {};
    }
    for (auto& desired : state->desired) {
        const auto cached = state->cache.find(desired.key);
        if (cached == state->cache.end() || !state->Reusable(cached->second)) desired.serviced = false;
        if (cached != state->cache.end()) cached->second.error.clear();
    }
    if (!state->started && state->documentMode && state->enabled && state->HasWork()) {
        std::thread([state] { state->Work(); }).detach(); state->started = true;
    }
    state->Notify(); state->changed.notify_one();
}

Progress Manager::Status() const {
    const auto state = state_; std::lock_guard lock(state->mutex);
    Progress progress; progress.limits = state->limits;
    if (state->stopping || !state->documentMode) return progress;
    progress.total = state->pages.size(); progress.paused = state->enabled && (state->resourcePaused || state->quotaPaused);
    progress.reason = progress.paused ? state->pauseReason : std::wstring{};
    progress.processing = state->enabled && !progress.paused && (state->documentActive || state->DocumentWork());
    for (const auto& [key, page] : state->pages) {
        if (page.verify) continue;
        const auto attempt = page.attempts.find(state->scale);
        if (attempt != page.attempts.end() && !attempt->second.error.empty()) { ++progress.failed; continue; }
        const auto cached = state->documentCache.find(state->CacheKey(page));
        if (cached != state->documentCache.end() && state->Reusable(cached->second)) ++progress.complete;
    }
    return progress;
}

Manager::~Manager() {
    std::lock_guard lock(state_->mutex);
    state_->window = nullptr;
    state_->stopping = true;
    if (state_->active) state_->active->cancelled->store(true);
    if (state_->documentActive) state_->documentActive->cancelled->store(true);
    state_->enabled = false;
    state_->desired.clear();
    state_->changed.notify_one();
}

void Manager::SetRequests(std::vector<Request> requests, const int scale) {
    const auto state = state_;
    std::lock_guard lock(state->mutex);
    if (state->stopping) return;
    state->documentMode = false; state->enabled = false;
    if (state->documentActive) state->documentActive->cancelled->store(true);
    std::set<std::wstring> seen;
    std::vector<State::Desired> desired;
    desired.reserve(requests.size());
    for (const auto& request : requests) {
        if (!request.source || request.key.empty() || !seen.insert(request.key).second) continue;
        auto& version = state->versions[request.key];
        if (!SameOwner(version.source, request.source)) {
            version.source = request.source;
            version.number = ++state->nextVersion;
        }
        State::Desired item{{request.key, version.number, scale}, request.source};
        const auto cached = state->cache.find(item.key);
        if (cached != state->cache.end() && (cached->second.image ||
            !cached->second.displayed.expired() || !cached->second.error.empty())) item.serviced = true;
        desired.push_back(std::move(item));
    }
    const bool sameViewport=desired.size()==state->desired.size() &&
        std::equal(desired.begin(),desired.end(),state->desired.begin(),
            [](const auto& left,const auto& right){return left.key==right.key;});
    if(sameViewport)for(std::size_t i=0;i<desired.size();++i)
        desired[i].serviced=desired[i].serviced || state->desired[i].serviced;
    state->desired = std::move(desired);
    state->scale = scale;
    if (state->active && state->FindDesired(state->active->key) == state->desired.end()) {
        state->active->cancelled->store(true);
    }
    if (!state->started && state->HasWork()) {
        // State, rather than Manager or App, owns the worker's entire lifetime.
        std::thread([state] { state->Work(); }).detach();
        state->started = true;
    }
    state->changed.notify_one();
}

void Manager::Cancel() {
    std::lock_guard lock(state_->mutex);
    state_->desired.clear();
    if (state_->active) state_->active->cancelled->store(true);
    state_->enabled = false; state_->visible.clear();
    if (state_->documentActive) state_->documentActive->cancelled->store(true);
    state_->changed.notify_one();
}

void Manager::Clear() {
    std::lock_guard lock(state_->mutex);
    state_->desired.clear();
    if (state_->active) state_->active->cancelled->store(true);
    state_->enabled = false; state_->visible.clear(); ++state_->epoch;
    if (state_->documentActive) state_->documentActive->cancelled->store(true);
    if (!state_->documentCache.empty()) state_->retiredDocuments.push_back(std::move(state_->documentCache));
    state_->documentCache.clear();
    for (auto& [key, page] : state_->pages) state_->retiredPages.push_back(std::move(page.page));
    state_->pages.clear(); state_->documentOrder.clear(); state_->documentMemory = state_->documentDisk = 0;
    state_->resourcePaused = state_->quotaPaused = false; state_->pauseReason.clear();
    if (!state_->cache.empty()) state_->retired.push_back(std::move(state_->cache));
    state_->cache.clear();
    state_->versions.clear();
    state_->memoryBytes = state_->diskBytes = 0;
    state_->changed.notify_one();
}

std::shared_ptr<ImageData> Manager::TryGet(const std::wstring& key, const int scale) const {
    std::lock_guard lock(state_->mutex);
    if (state_->stopping || state_->scale != scale) return {};
    if (state_->documentMode) {
        if (!state_->enabled) return {};
        const auto page = state_->pages.find(key);
        if (page == state_->pages.end() || page->second.verify) return {};
        const auto entry = state_->documentCache.find(state_->CacheKey(page->second));
        if (entry == state_->documentCache.end()) return {};
        auto image = entry->second.image ? entry->second.image : entry->second.displayed.lock();
        if (image) entry->second.memoryUsed = entry->second.diskUsed = ++state_->clock;
        return image;
    }
    auto item = std::find_if(state_->desired.begin(), state_->desired.end(), [&](const auto& desired) {
        return desired.key.page == key && desired.key.scale == scale;
    });
    if (item == state_->desired.end()) return {};
    auto entry = state_->cache.find(item->key);
    if (entry == state_->cache.end()) return {};
    auto image = entry->second.image ? entry->second.image : entry->second.displayed.lock();
    if (image) {
        entry->second.memoryUsed = entry->second.diskUsed = ++state_->clock;
        return image;
    }
    // Rendering is read-only. An over-budget stationary viewport must not cycle
    // through PNG reloads; a changed desired viewport explicitly schedules them.
    return {};
}

std::wstring Manager::ErrorFor(const std::wstring& key, const int scale) const {
    std::lock_guard lock(state_->mutex);
    if (state_->stopping || state_->scale != scale) return {};
    if (state_->documentMode) {
        if (!state_->enabled) return {};
        const auto page = state_->pages.find(key);
        if (page == state_->pages.end() || page->second.verify) return {};
        const auto attempt = page->second.attempts.find(scale);
        return attempt == page->second.attempts.end() ? std::wstring{} : attempt->second.error;
    }
    const auto item = std::find_if(state_->desired.begin(), state_->desired.end(), [&](const auto& desired) {
        return desired.key.page == key && desired.key.scale == scale;
    });
    if (item == state_->desired.end()) return {};
    const auto entry = state_->cache.find(item->key);
    return entry == state_->cache.end() ? std::wstring{} : entry->second.error;
}

bool Manager::Pending() const {
    std::lock_guard lock(state_->mutex);
    return !state_->stopping && state_->HasWork();
}
}
