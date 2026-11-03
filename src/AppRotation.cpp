#include "App.h"

void App::RotateCurrentPage(const int delta) {
    if(folder_.Empty())return;
    const auto key=folder_.PageKey(folder_.Index());
    const auto found=pageRotations_.find(key);
    const int previous=found==pageRotations_.end()?0:found->second;
    const int turns=delta==0?0:((previous+delta)%4+4)%4;
    std::wstring error;
    if(!cache_.SetRotation(folder_.Current().wstring(),turns,error)) {
        SetNotice(error,6000);return;
    }
    if(turns)pageRotations_[key]=turns;else pageRotations_.erase(key);
    currentImage_.reset();enhancedImage_.reset();bitmap_.Reset();readerTiles_.clear();
    aiDocumentDirty_=true;
    RefreshAiRequests();
    showOriginalForComparison_=false;++enhancementGeneration_;
    zoomFactor_=1;pan_={0,0};
    if(continuous_)ResetReaderSizingReference();
#if EINZIGVIEWER_WITH_RTX
    if(hdrPresenter_)hdrPresenter_->Invalidate();
#endif
    ApplyCurrentIfReady();
    UpdateWindowTitle();InvalidateRect(window_,nullptr,FALSE);
}
