#include "ImageCache.h"

#include "WicDecoder.h"
#include "ImageRotation.h"

#include <algorithm>

ImageCache::ImageCache(const std::size_t maximumBytes) : maximumBytes_(maximumBytes) {
    const unsigned int hardwareThreads = std::thread::hardware_concurrency();
    const unsigned int workerCount = std::clamp(hardwareThreads > 1 ? hardwareThreads / 2 : 2U, 2U, 4U);
    workers_.reserve(workerCount);
    for (unsigned int index = 0; index < workerCount; ++index) {
        workers_.emplace_back(&ImageCache::WorkerLoop, this);
    }
}

ImageCache::~ImageCache() {
    {
        std::scoped_lock lock(mutex_);
        stopping_ = true;
    }
    condition_.notify_all();
    for (auto& worker : workers_) {
        if (worker.joinable()) worker.join();
    }
}

void ImageCache::SetNotificationWindow(const HWND window) {
    std::scoped_lock lock(mutex_);
    notificationWindow_ = window;
}

void ImageCache::Request(const std::wstring& path, const int priority) {
    if (path.empty()) return;
    {
        std::scoped_lock lock(mutex_);
        if (cache_.contains(path) || pending_.contains(path) || errors_.contains(path)) return;
        pending_.insert(path);
        errors_.erase(path);
        tasks_.push(Task{path, priority, sequence_++, sourceLease_, prepare_});
    }
    condition_.notify_one();
}

void ImageCache::RetryFailed(const std::wstring& path,const int priority) {
    {
        std::scoped_lock lock(mutex_);
        if(!errors_.contains(path) || cache_.contains(path) || pending_.contains(path))return;
        errors_.erase(path);
        pending_.insert(path);
        tasks_.push(Task{path,priority,sequence_++,sourceLease_,prepare_});
    }
    condition_.notify_one();
}

void ImageCache::StorePrepared(std::shared_ptr<ImageData> image) {
    if(!image || image->path.empty() || !image->width || !image->height || image->pixels.empty())return;
    std::scoped_lock lock(mutex_);
    const auto path=image->path;
    if(const auto old=cache_.find(path);old!=cache_.end())currentBytes_-=old->second.image->ByteSize();
    currentBytes_+=image->ByteSize();
    cache_[path]=CacheEntry{std::move(image),++access_,0};
    errors_.erase(path);
    TrimLocked();
}

std::shared_ptr<ImageData> ImageCache::TryGet(const std::wstring& path) {
    std::scoped_lock lock(mutex_);
    const auto found = cache_.find(path);
    if (found == cache_.end()) return {};
    found->second.access = ++access_;
    return found->second.image;
}

void ImageCache::SetSourceLease(std::shared_ptr<void> lease, Preparation prepare) {
    ClearQueued();
    std::scoped_lock lock(mutex_);
    sourceLease_ = std::move(lease);
    prepare_ = std::move(prepare);
    errors_.clear();
}

std::wstring ImageCache::ErrorFor(const std::wstring& path) const {
    std::scoped_lock lock(mutex_);
    const auto found = errors_.find(path);
    return found == errors_.end() ? std::wstring{} : found->second;
}

void ImageCache::Pin(const std::vector<std::wstring>& paths) {
    std::scoped_lock lock(mutex_);
    pinned_.clear();
    pinned_.insert(paths.begin(), paths.end());
    TrimLocked();
}

void ImageCache::ClearQueued() {
    std::scoped_lock lock(mutex_);
    while (!tasks_.empty()) {
        pending_.erase(tasks_.top().path);
        tasks_.pop();
    }
}

std::size_t ImageCache::CurrentBytes() const {
    std::scoped_lock lock(mutex_);
    return currentBytes_;
}

bool ImageCache::SetRotation(const std::wstring& path,const int clockwiseQuarterTurns,std::wstring& error) {
    const int turns=(clockwiseQuarterTurns%4+4)%4;
    std::scoped_lock lock(mutex_);
    const auto found=cache_.find(path);
    if(found!=cache_.end() && found->second.rotation!=turns) {
        auto rotated=RotateImage(*found->second.image,turns-found->second.rotation,error);
        if(!rotated)return false;
        currentBytes_-=found->second.image->ByteSize();
        found->second.image=std::move(rotated);found->second.rotation=turns;
        currentBytes_+=found->second.image->ByteSize();
    }
    if(turns)rotations_[path]=turns;else rotations_.erase(path);
    TrimLocked();return true;
}

void ImageCache::WorkerLoop() {
    for (;;) {
        Task task;
        {
            std::unique_lock lock(mutex_);
            condition_.wait(lock, [&] { return stopping_ || !tasks_.empty(); });
            if (stopping_) return;
            task = tasks_.top();
            tasks_.pop();
        }

        std::wstring error;
        std::shared_ptr<ImageData> image;
        try {
            if(!task.prepare || task.prepare(task.path,error))image = WicDecoder::Decode(task.path, error);
        }
        catch (...) { error = L"圖片解碼失敗或記憶體不足。 / Image decoding failed or ran out of memory."; }
        HWND notify = nullptr;
        {
            std::scoped_lock lock(mutex_);
            pending_.erase(task.path);
            const auto orientation=rotations_.find(task.path);
            const int rotation=orientation==rotations_.end()?0:orientation->second;
            if(image && rotation)image=RotateImage(*image,rotation,error);
            if (image) {
                const auto bytes = image->ByteSize();
                const auto existing = cache_.find(task.path);
                if (existing != cache_.end()) currentBytes_ -= existing->second.image->ByteSize();
                cache_[task.path] = CacheEntry{std::move(image), ++access_,rotation};
                currentBytes_ += bytes;
                errors_.erase(task.path);
                TrimLocked();
            } else {
                errors_[task.path] = error.empty() ? L"無法解碼圖片" : std::move(error);
            }
            notify = notificationWindow_;
        }
        if (notify && IsWindow(notify)) PostMessageW(notify, ImageReadyMessage, 0, 0);
    }
}

void ImageCache::TrimLocked() {
    while (currentBytes_ > maximumBytes_) {
        auto oldest = cache_.end();
        for (auto iterator = cache_.begin(); iterator != cache_.end(); ++iterator) {
            if (oldest == cache_.end() ||
                (pinned_.contains(oldest->first) && !pinned_.contains(iterator->first)) ||
                (pinned_.contains(oldest->first) == pinned_.contains(iterator->first) && iterator->second.access < oldest->second.access)) oldest = iterator;
        }
        if (oldest == cache_.end()) break;
        currentBytes_ -= oldest->second.image->ByteSize();
        cache_.erase(oldest);
    }
}
