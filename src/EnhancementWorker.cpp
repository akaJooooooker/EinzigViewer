#include "EnhancementWorker.h"

#include <exception>
#include <string>

EnhancementWorker::EnhancementWorker(ImageEnhancer& enhancer, const HWND notificationWindow)
    : enhancer_(enhancer), notificationWindow_(notificationWindow),
      worker_(&EnhancementWorker::WorkerLoop, this) {}

EnhancementWorker::~EnhancementWorker() {
    {
        std::scoped_lock lock(mutex_);
        stopping_ = true;
        pending_ = {};
        hasPending_ = false;
    }
    condition_.notify_all();
    if (worker_.joinable()) worker_.join();
}

void EnhancementWorker::Request(std::shared_ptr<ImageData> source,
    const std::uint32_t targetWidth, const std::uint32_t targetHeight,
    const std::uint64_t generation) {
    {
        std::scoped_lock lock(mutex_);
        pending_ = WorkItem{std::move(source), targetWidth, targetHeight, generation};
        hasPending_ = true;
    }
    condition_.notify_one();
}

EnhancementWorker::Result EnhancementWorker::TakeResult() {
    std::scoped_lock lock(mutex_);
    Result result = std::move(result_);
    result_ = {};
    return result;
}

void EnhancementWorker::WorkerLoop() {
    for (;;) {
        WorkItem work;
        {
            std::unique_lock lock(mutex_);
            condition_.wait(lock, [&] { return stopping_ || hasPending_; });
            if (stopping_) return;
            work = std::move(pending_);
            pending_ = {};
            hasPending_ = false;
        }

        Result completed;
        completed.generation = work.generation;
        try {
            if (work.source) {
                completed.image = enhancer_.Enhance(
                    *work.source, work.targetWidth, work.targetHeight, completed.error);
            } else {
                completed.error = L"RTX 工作缺少來源圖片。 / RTX work item has no source image.";
            }
        } catch (const std::exception& exception) {
            completed.error = L"RTX 處理發生例外，但 Eidolyx 已安全回復一般顯示。 / "
                              L"RTX processing raised an exception; Eidolyx safely returned to normal display. ";
            const std::string what = exception.what();
            completed.error.append(what.begin(), what.end());
        } catch (...) {
            completed.error = L"RTX 處理發生未知錯誤，但 Eidolyx 已安全回復一般顯示。 / "
                              L"RTX processing failed unexpectedly; Eidolyx safely returned to normal display.";
        }

        {
            std::scoped_lock lock(mutex_);
            result_ = std::move(completed);
        }
        if (notificationWindow_ && IsWindow(notificationWindow_)) {
            PostMessageW(notificationWindow_, EnhancementReadyMessage, 0, 0);
        }
    }
}

