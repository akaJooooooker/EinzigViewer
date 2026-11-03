#include "App.h"
#include "ImageClipboard.h"
#include "ImageComposition.h"
#include <d2d1helper.h>
#include <algorithm>
#include <cmath>

namespace {
POINT ClampPoint(POINT point,const RECT& viewport) {
    return {std::clamp(point.x,viewport.left,viewport.right),std::clamp(point.y,viewport.top,viewport.bottom)};
}
RECT DragRect(POINT from,POINT to) {
    return {std::min(from.x,to.x),std::min(from.y,to.y),std::max(from.x,to.x),std::max(from.y,to.y)};
}
bool Overlaps(const RECT& a,const RECT& b) {
    return a.left<b.right && a.right>b.left && a.top<b.bottom && a.bottom>b.top;
}
}

bool App::WriteClipboardImage(const ImageData& image) {
    std::wstring error;bool copied=false;
    try {
#ifdef EINZIGVIEWER_TESTING
        if(clipboardWriterForTest_)copied=clipboardWriterForTest_(image,error);
        else
#endif
        copied=image_clipboard::Copy(window_,image,error);
    }catch(...){error=L"無法複製圖片，可能記憶體不足。 / Could not copy the image; memory may be insufficient.";}
    if(!copied)SetNotice(error.empty()?(IsEnglish()?L"Could not copy the image.":L"無法複製圖片。"):error,6000);
    return copied;
}

void App::CopyCurrentImage() {
    CancelRegionCopy();
    if(folder_.Empty())return;
    auto image=currentImage_;
    if(!image || image->path!=folder_.Current().wstring())image=cache_.TryGet(folder_.Current().wstring());
    if(!image){SetNotice(IsEnglish()?L"Wait for the page to finish loading before copying.":L"請等此頁載入完成後再複製。",5000);return;}
    if(WriteClipboardImage(*image))SetNotice(IsEnglish()?L"Image copied.":L"已複製目前圖片。");
}

void App::BeginRegionCopy() {
    CancelRegionCopy();
    if(folder_.Empty())return;
    const bool wasHdr=hdrEnabled_;
    if(wasHdr)DisableHdrMode();
    if(continuous_)RefreshContinuous();
    if(!EnsureRenderTarget())return;
    RECT client{};GetClientRect(window_,&client);
    const double dpi=static_cast<double>(GetDpiForWindow(window_))/96;
    const auto top=static_cast<LONG>(std::lround((showInfo_?48:0)*dpi));
    const auto bottom=client.bottom-static_cast<LONG>(std::lround((showInfo_?38:0)*dpi));
    if(client.right<=0 || bottom<=top)return;
    regionViewport_={0,top,client.right,std::max(top,bottom)};
    std::vector<image_composition::Layer> layers;
    const auto add=[&](std::shared_ptr<ImageData> image,image_composition::Bounds bounds) {
        RECT occupied{static_cast<LONG>(std::floor(std::clamp(bounds.left,0.0,static_cast<double>(client.right)))),
            static_cast<LONG>(std::floor(std::clamp(bounds.top,static_cast<double>(top),static_cast<double>(bottom)))),
            static_cast<LONG>(std::ceil(std::clamp(bounds.right,0.0,static_cast<double>(client.right)))),
            static_cast<LONG>(std::ceil(std::clamp(bounds.bottom,static_cast<double>(top),static_cast<double>(bottom))))};
        if(!Overlaps(occupied,regionViewport_))return;
        if(image)layers.push_back({std::move(image),bounds});else regionMissing_.push_back(occupied);
    };
    if(continuous_) {
        const double width=ReaderWidth();const auto size=ReaderSize();
        const float limit=static_cast<float>(std::max(0.0,(width-size.width)/2));
        const double left=(size.width-width)/2+std::clamp(readerPanX_,-limit,limit);
        const auto first=layout_.PageAt(scrollPosition_);
        const auto last=std::min({folder_.Size()-1,first+127,layout_.PageAt(scrollPosition_+ReaderHeight()/width)});
        for(auto i=first;i<=last;++i) {
            auto image=i==folder_.Index()?currentImage_:cache_.TryGet(folder_.At(i).wstring());
            image=AiDisplayImage(i,image);
            add(std::move(image),{left*dpi,top+(layout_.Top(i)-scrollPosition_)*width*dpi,
                (left+width)*dpi,top+(layout_.Top(i+1)-scrollPosition_)*width*dpi});
        }
        const double remainingTop=top+(layout_.Top(last+1)-scrollPosition_)*width*dpi;
        if(last+1<folder_.Size() && remainingTop<bottom)
            add({}, {left*dpi,remainingTop,(left+width)*dpi,static_cast<double>(bottom)});
    }else {
        auto image=enhancementEnabled_ && enhancedImage_ && !showOriginalForComparison_?enhancedImage_:AiDisplayImage(folder_.Index(),currentImage_);
        if(image) {
            const auto bounds=ImageRectangle(renderTarget_->GetSize());
            add(std::move(image),{bounds.left*dpi,bounds.top*dpi,bounds.right*dpi,bounds.bottom*dpi});
        }
    }
    if(layers.empty()){regionMissing_.clear();SetNotice(IsEnglish()?L"Wait for the image to finish loading before selecting.":L"請等圖片載入完成後再框選。",5000);return;}
    std::wstring error;
    regionSnapshot_=image_composition::Compose(layers,regionViewport_,regionViewport_,error);
    if(!regionSnapshot_){regionMissing_.clear();SetNotice(error,6000);return;}
    const auto properties=D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),96,96);
    const auto result=renderTarget_->CreateBitmap(D2D1::SizeU(regionSnapshot_->width,regionSnapshot_->height),
        regionSnapshot_->pixels.data(),regionSnapshot_->stride,properties,&regionBitmap_);
    if(FAILED(result)){CancelRegionCopy();SetNotice(IsEnglish()?L"Could not prepare region selection.":L"無法準備框選畫面。",5000);return;}
    selectingRegion_=true;regionDragging_=false;regionStart_=regionEnd_={0,top};
    ShowFullscreenCursor();KillTimer(window_,4);SetCursor(LoadCursorW(nullptr,IDC_CROSS));
    SetNotice(wasHdr?(IsEnglish()?L"Region copy uses SDR. Drag to copy; Esc or right-click cancels.":L"框選複製使用 SDR。拖曳框選後複製；Esc 或右鍵取消。"):
        (IsEnglish()?L"Drag to copy a region; Esc or right-click cancels.":L"拖曳框選要複製的範圍；Esc 或右鍵取消。"),60000);
    InvalidateRect(window_,nullptr,FALSE);
}

void App::CancelRegionCopy() {
    const bool active=selectingRegion_;
    selectingRegion_=regionDragging_=false;regionSnapshot_.reset();regionBitmap_.Reset();regionMissing_.clear();
    if(active) {
        if(GetCapture()==window_)ReleaseCapture();
        SetCursor(LoadCursorW(nullptr,IDC_ARROW));notice_.clear();
        if(fullscreen_ && IsWindow(window_))ArmFullscreenCursorHide();
        InvalidateRect(window_,nullptr,FALSE);
    }
}

void App::FinishRegionCopy(const POINT point) {
    if(!selectingRegion_ || !regionDragging_)return;
    regionEnd_=ClampPoint(point,regionViewport_);
    const auto selection=DragRect(regionStart_,regionEnd_);
    if(selection.right-selection.left<2 || selection.bottom-selection.top<2){CancelRegionCopy();return;}
    for(const auto& missing:regionMissing_)if(Overlaps(selection,missing)) {
        CancelRegionCopy();SetNotice(IsEnglish()?L"Part of this region is not displayed yet. Wait for loading or zoom in, then retry.":L"選取範圍尚未完整顯示，請等載入或放大後再框選。",6000);return;
    }
    const auto snapshot=regionSnapshot_;const auto viewport=regionViewport_;
    CancelRegionCopy();std::wstring error;
    auto image=image_composition::Compose({{snapshot,{static_cast<double>(viewport.left),static_cast<double>(viewport.top),
        static_cast<double>(viewport.right),static_cast<double>(viewport.bottom)}}},selection,viewport,error);
    if(!image){SetNotice(error,6000);return;}
    if(WriteClipboardImage(*image))SetNotice(IsEnglish()?L"Selected region copied.":L"已複製選取區域。");
}

void App::DrawRegionSelection() {
    if(!regionBitmap_)return;
    const float dpi=static_cast<float>(GetDpiForWindow(window_))/96;
    renderTarget_->DrawBitmap(regionBitmap_.Get(),D2D1::RectF(regionViewport_.left/dpi,regionViewport_.top/dpi,
        regionViewport_.right/dpi,regionViewport_.bottom/dpi));
    if(regionDragging_) {
        const auto selection=DragRect(regionStart_,regionEnd_);
        renderTarget_->DrawRectangle(D2D1::RectF(selection.left/dpi,selection.top/dpi,selection.right/dpi,selection.bottom/dpi),accentBrush_.Get(),1.5F);
    }
}
