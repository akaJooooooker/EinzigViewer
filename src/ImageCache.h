#pragma once

#include "ImageData.h"

#include <Windows.h>

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <functional>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class ImageCache {
public:
    static constexpr UINT ImageReadyMessage = WM_APP + 1;

    explicit ImageCache(std::size_t maximumBytes = 768ULL * 1024ULL * 1024ULL);
    ~ImageCache();

    ImageCache(const ImageCache&) = delete;
    ImageCache& operator=(const ImageCache&) = delete;

    void SetNotificationWindow(HWND window);
    using Preparation = std::function<bool(const std::wstring&, std::wstring&)>;
    void SetSourceLease(std::shared_ptr<void> lease, Preparation prepare = {});
    void Request(const std::wstring& path, int priority);
    void RetryFailed(const std::wstring& path,int priority);
    void StorePrepared(std::shared_ptr<ImageData> image);
    [[nodiscard]] std::shared_ptr<ImageData> TryGet(const std::wstring& path);
    [[nodiscard]] std::wstring ErrorFor(const std::wstring& path) const;
    void Pin(const std::vector<std::wstring>& paths);
    void ClearQueued();
    bool SetRotation(const std::wstring& path,int clockwiseQuarterTurns,std::wstring& error);
    [[nodiscard]] std::size_t CurrentBytes() const;

private:
    struct CacheEntry {
        std::shared_ptr<ImageData> image;
        std::uint64_t access = 0;
        int rotation = 0;
    };

    struct Task {
        std::wstring path;
        int priority = 0;
        std::uint64_t sequence = 0;
        std::shared_ptr<void> sourceLease;
        Preparation prepare;
    };

    struct TaskLess {
        bool operator()(const Task& left, const Task& right) const noexcept {
            if (left.priority != right.priority) return left.priority < right.priority;
            return left.sequence > right.sequence;
        }
    };

    void WorkerLoop();
    void TrimLocked();

    const std::size_t maximumBytes_;
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::priority_queue<Task, std::vector<Task>, TaskLess> tasks_;
    std::unordered_set<std::wstring> pending_;
    std::unordered_set<std::wstring> pinned_;
    std::unordered_map<std::wstring, CacheEntry> cache_;
    std::unordered_map<std::wstring, std::wstring> errors_;
    std::vector<std::thread> workers_;
    std::size_t currentBytes_ = 0;
    std::uint64_t sequence_ = 0;
    std::uint64_t access_ = 0;
    HWND notificationWindow_ = nullptr;
    bool stopping_ = false;
    std::shared_ptr<void> sourceLease_;
    Preparation prepare_;
    std::unordered_map<std::wstring,int> rotations_;
};

