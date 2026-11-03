#include "App.h"
#include "AiResourceDialog.h"

#include <algorithm>

std::wstring App::AiKey(const std::size_t index) const {
    if(index>=folder_.Size())return {};
    const auto page=folder_.PageKey(index);
    const auto rotation=pageRotations_.find(page);
    return std::to_wstring(aiDocumentGeneration_)+L"|"+page+L"|"+
        std::to_wstring(rotation==pageRotations_.end()?0:rotation->second)+L"|realcugan-20220728-models-se-n0";
}

std::shared_ptr<ImageData> App::AiDisplayImage(const std::size_t index,
    const std::shared_ptr<ImageData>& original) const {
    if(aiEnabled_ && aiManager_ && original && !showOriginalForComparison_) {
        if(const auto found=aiDisplayedPages_.find(AiKey(index));found!=aiDisplayedPages_.end()) {
            const auto source=found->second.source.lock();
            if(source==original || (!original->sourceFingerprint.empty() &&
                found->second.image->sourceFingerprint==original->sourceFingerprint))return found->second.image;
        }
    }
    return original;
}

void App::ToggleAi() {
    if(aiEnabled_) {
        aiEnabled_=false;
        if(aiManager_)aiManager_->Cancel();
    }else {
        std::wstring settingsError;
        if(!ai::ValidateUserLimits(aiUserLimits_,settingsError)) {
            SetNotice((IsEnglish()?L"AI kept the original. Open AI → Resource settings: ":L"AI 保留原圖。請開啟 AI → 資源設定：")+settingsError,10000);
            return;
        }
        if(folder_.Empty()) {
            SetNotice(IsEnglish()?L"Open an image before enabling AI.":L"請先開啟圖片，再啟動 AI。",5000);
            return;
        }
        if(hdrEnabled_)DisableHdrMode();
        enhancementEnabled_=enhancementInProgress_=false;
        enhancedImage_.reset();++enhancementGeneration_;
        if(!aiManager_) {
            ai::RuntimeOptions options;
            const auto directory=updates::ExecutableDirectory()/L"ai"/L"realcugan";
            options.executable=directory/L"realcugan-ncnn-vulkan.exe";
            options.modelDirectory=directory/L"models-se";
            if(renderTarget_)options.maximumDimension=std::min(16384U,renderTarget_->GetMaximumBitmapSize());
            std::error_code error;
            const auto temporary=std::filesystem::temp_directory_path(error);
            if(error) {
                SetNotice(IsEnglish()?L"AI could not access the temporary folder.":L"AI 無法存取暫存資料夾。",6000);
                return;
            }
            ai::Executor executor;
            ai::ResourceProbe resources;
            ai::BatchExecutor batch;
#ifdef EINZIGVIEWER_TESTING
            executor=aiExecutorForTest_;
            resources=aiResourceProbeForTest_;batch=aiBatchExecutorForTest_;
#endif
            aiManager_=std::make_unique<ai::Manager>(window_,std::move(options),temporary/L"EinzigViewer-AI",
                std::move(executor),std::move(resources),std::move(batch));
            aiManager_->SetUserLimits(aiUserLimits_);
        }
        aiDocumentDirty_=true;
        aiEnabled_=true;
    }
    if(!aiEnabled_)aiDisplayedPages_.clear();
    aiLastError_.clear();showOriginalForComparison_=false;
    RefreshAiRequests();readerTiles_.clear();UploadCurrentBitmap();
    UpdateMenuChecks();UpdateWindowTitle();InvalidateRect(window_,nullptr,FALSE);
}

void App::ShowAiResourceSettings() {
    std::error_code error;
    const auto temporary=std::filesystem::temp_directory_path(error);
    if(error) {
        SetNotice(IsEnglish()?L"Could not access the temporary folder for AI resource settings.":L"AI 資源設定無法存取暫存資料夾。",7000);
        return;
    }
    ai::ResourceProbe probe;
#ifdef EINZIGVIEWER_TESTING
    probe=aiResourceProbeForTest_;
#endif
    if(const auto limits=ai::ShowResourceDialog(window_,IsEnglish(),aiUserLimits_,temporary,std::move(probe)))
        ApplyAiResourceSettings(*limits);
}

bool App::ApplyAiResourceSettings(const ai::UserLimits& limits) {
    std::wstring error;
    if(!ai::ValidateUserLimits(limits,error) || !ai::WriteUserLimits(SettingsPath(),limits,error)) {
        SetNotice((IsEnglish()?L"AI resource settings were not applied: ":L"AI 資源設定未套用：")+error,10000);
        return false;
    }
    aiUserLimits_=limits;
    if(aiManager_)aiManager_->SetUserLimits(aiUserLimits_);
    RefreshAiRequests();
    readerTiles_.clear();UploadCurrentBitmap();
    UpdateMenuChecks();UpdateWindowTitle();InvalidateRect(window_,nullptr,FALSE);
    return true;
}

void App::SetAiScale(const int scale) {
    if((scale!=2 && scale!=4) || scale==aiScale_)return;
    aiScale_=scale;aiDisplayedPages_.clear();aiLastError_.clear();showOriginalForComparison_=false;
    RefreshAiRequests();readerTiles_.clear();UploadCurrentBitmap();
    UpdateMenuChecks();UpdateWindowTitle();InvalidateRect(window_,nullptr,FALSE);
}

void App::ResetAiDocument() {
    aiEnabled_=false;++aiDocumentGeneration_;aiLastError_.clear();aiDisplayedPages_.clear();
    aiDocumentDirty_=true;
    if(aiManager_)aiManager_->Clear();
    readerTiles_.clear();
}

void App::RefreshAiRequests() {
    if(!aiEnabled_ || !aiManager_)return;
    RefreshAiDocument();
    std::vector<ai::Request> requests;
    const auto add=[&](const std::size_t index) {
        auto source=index==folder_.Index()?currentImage_:cache_.TryGet(folder_.At(index).wstring());
        if(source && source->path!=folder_.At(index).wstring())source.reset();
        // A key without a decoded image still promotes lazy preparation of the current page.
        requests.push_back({AiKey(index),std::move(source)});
    };
    if(!folder_.Empty()) {
        add(folder_.Index());
        if(continuous_) {
            const auto first=layout_.PageAt(scrollPosition_);
            const auto last=std::min({folder_.Size()-1,first+127,layout_.PageAt(scrollPosition_+ReaderHeight()/ReaderWidth())});
            for(auto i=first;i<=last;++i)if(i!=folder_.Index())add(i);
        }
    }
    aiManager_->SetPriority(requests,aiScale_);
    UpdateAiDisplayImages(requests);
}

void App::RefreshAiDocument() {
    if(!aiDocumentDirty_ || !aiManager_)return;
    std::vector<ai::Page> pages;pages.reserve(folder_.Size());
    const auto lease=folder_.SourceLease();const auto prepare=folder_.Preparation(true);
    for(std::size_t i=0;i<folder_.Size();++i) {
        const auto rotation=pageRotations_.find(folder_.PageKey(i));
        pages.push_back({AiKey(i),folder_.At(i),rotation==pageRotations_.end()?0:rotation->second,
            lease,prepare,i==folder_.Index()?currentImage_:cache_.TryGet(folder_.At(i).wstring())});
    }
    aiManager_->SetDocument(std::move(pages));aiDocumentDirty_=false;
}

void App::UpdateAiDisplayImages(const std::vector<ai::Request>& desired) {
    // Display ownership survives LRU eviction, but is bounded independently of
    // how many tiny pages fit on screen. Current page has first priority.
    const auto budget=aiManager_->Status().limits.displayBytes;
    std::size_t bytes=0;
    std::map<std::wstring,AiDisplayedPage> displayed;
    for(const auto& request:desired) {
        if(!request.source)continue;
        std::shared_ptr<ImageData> image;
        const auto previous=aiDisplayedPages_.find(request.key);
        if(previous!=aiDisplayedPages_.end() && (previous->second.source.lock()==request.source ||
            (!request.source->sourceFingerprint.empty() && previous->second.image->sourceFingerprint==request.source->sourceFingerprint)))
            image=previous->second.image;
        if(!image)image=aiManager_->TryGet(request.key,aiScale_);
        if(image && !request.source->sourceFingerprint.empty() && image->sourceFingerprint!=request.source->sourceFingerprint)image.reset();
        if(image && image->ByteSize()<=budget-bytes) {
            bytes+=image->ByteSize();displayed.emplace(request.key,AiDisplayedPage{request.source,std::move(image)});
        }
    }
    aiDisplayedPages_.swap(displayed);
}

void App::ApplyAiResult() {
    if(!aiEnabled_ || !aiManager_)return;
    RefreshAiRequests();
    // Pull only matching desired results; selections retain their own snapshot.
    readerTiles_.clear();UploadCurrentBitmap();
    if(!folder_.Empty()) {
        const auto error=aiManager_->ErrorFor(AiKey(folder_.Index()),aiScale_);
        if(!error.empty() && error!=aiLastError_) {
            aiLastError_=error;
            SetNotice((IsEnglish()?L"AI kept the original: ":L"AI 保留原圖：")+error,8000);
        }
    }
    UpdateWindowTitle();InvalidateRect(window_,nullptr,FALSE);
}

std::wstring App::AiStatus() const {
    if(!aiEnabled_)return {};
    const auto prefix=L"AI "+std::to_wstring(aiScale_)+L"×";
    if(showOriginalForComparison_)return prefix+(IsEnglish()?L" · Original comparison":L" · 原圖比較");
    if(aiManager_ && !folder_.Empty()) {
        const auto progress=aiManager_->Status();
        const auto count=L" · "+std::to_wstring(progress.complete)+L"/"+std::to_wstring(progress.total);
        const auto failed=progress.failed?L" · "+std::to_wstring(progress.failed)+(IsEnglish()?L" failed":L" 頁失敗"):L"";
        if(progress.paused)return prefix+count+failed+(IsEnglish()?L" · Paused: ":L" · 暫停：")+progress.reason;
        if(const auto image=AiDisplayImage(folder_.Index(),currentImage_);image && image!=currentImage_)
            return prefix+count+failed+L" · "+std::to_wstring(image->width)+L" × "+std::to_wstring(image->height)+
                (progress.processing?(IsEnglish()?L" · Preprocessing":L" · 預處理中"):L"");
        if(!aiManager_->ErrorFor(AiKey(folder_.Index()),aiScale_).empty())
            return prefix+count+failed+(IsEnglish()?L" · Original":L" · 保留原圖");
        if(aiManager_->Pending())return prefix+(IsEnglish()?L" · Processing; original":L" · 處理中，顯示原圖");
    }
    return prefix+(currentImage_?(IsEnglish()?L" · Original":L" · 原圖"):
        (IsEnglish()?L" · Waiting for original":L" · 等待原圖載入"));
}
