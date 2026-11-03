#include "App.h"
#include <d2d1helper.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <set>
#include <utility>

namespace { constexpr UINT_PTR BookTimer = 5; }

void App::StartBookOpen(const std::filesystem::path& file) {
    if (bookFuture_.valid()) {
        bookCancelled_->store(true);
        pendingBook_ = file;
        return;
    }
    bookCancelled_ = std::make_shared<std::atomic_bool>(false);
    auto cancelled = bookCancelled_;
    try {
        bookFuture_ = std::async(std::launch::async, [file, cancelled] {
            BookOpenResult result;
            result.book = EpubBook::Open(file, *cancelled, result.error);
            return result;
        });
        SetTimer(window_, BookTimer, 100, nullptr);
        SetNotice(IsEnglish() ? L"Opening EPUB in the background… Esc cancels." : L"正在背景開啟 EPUB… 按 Esc 取消。", 60000);
    } catch (...) {
        SetNotice(IsEnglish() ? L"Could not start EPUB loading." : L"無法開始載入 EPUB。", 6000);
    }
}

void App::PollBookOpen() {
    if (!bookFuture_.valid() || bookFuture_.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) return;
    KillTimer(window_, BookTimer);
    auto result = bookFuture_.get();
    const bool cancelled = bookCancelled_->load();
    bookCancelled_.reset();
    if (!pendingBook_.empty()) {
        const auto next = std::exchange(pendingBook_, {});
        StartBookOpen(next);
        return;
    }
    notice_.clear();
    if (!cancelled && result.book) {
        folder_.OpenBook(std::move(result.book));
        ResetDocument();
        SetContinuous(true);
    } else if (!cancelled) {
        MessageBoxW(window_, result.error.c_str(), IsEnglish() ? L"Could not open EPUB" : L"無法開啟 EPUB", MB_OK | MB_ICONERROR);
    }
    InvalidateRect(window_, nullptr, FALSE);
}

void App::ResetDocument() {
    CancelRegionCopy();
    ResetAiDocument();
    cache_.SetSourceLease(folder_.SourceLease(),folder_.Preparation());
    for(std::size_t i=0;i<folder_.Size();++i) {
        const auto rotation=pageRotations_.find(folder_.PageKey(i));
        if(rotation!=pageRotations_.end()) {std::wstring ignored;cache_.SetRotation(folder_.At(i).wstring(),rotation->second,ignored);}
    }
    currentImage_.reset(); enhancedImage_.reset(); bitmap_.Reset(); readerTiles_.clear();
    readerTileSources_.clear();
    showOriginalForComparison_ = false;
    ++enhancementGeneration_;
    zoomFactor_ = 1; readerZoom_ = 1; readerPanX_ = 0;
    pan_ = {0, 0}; loading_ = true;
    layout_.Reset(folder_.Size()); layout_.Rebuild();
    ResetReaderSizingReference();
    scrollPosition_ = folder_.Empty() ? 0 : layout_.Top(folder_.Index());
    requestedFirst_ = requestedLast_ = SIZE_MAX;
    if (continuous_) RefreshContinuous();
    else { ScheduleNeighborhood(); ApplyCurrentIfReady(); }
    UpdateWindowTitle();
    UpdateMenuChecks();
    InvalidateRect(window_, nullptr, FALSE);
}

D2D1_SIZE_F App::ReaderSize() const {
    RECT rect{}; GetClientRect(window_, &rect);
    const float dpi = static_cast<float>(GetDpiForWindow(window_)) / 96.0F;
    return D2D1::SizeF(std::max(1.0F, rect.right / dpi), std::max(1.0F, rect.bottom / dpi));
}
double App::ReaderWidth() const {
    const double available=std::max(1.0,static_cast<double>(ReaderSize().width)-24);
    const double dpi=static_cast<double>(GetDpiForWindow(window_))/96.0;
    const double actual=readerSourceWidth_?readerSourceWidth_/dpi:available;
    const double ratio=readerSourceWidth_ && readerSourceHeight_?
        static_cast<double>(readerSourceHeight_)/readerSourceWidth_:1.41421356;
    double width=available;
    if(readerViewMode_==ViewMode::FitWindow)width=std::min({available,ReaderHeight()/ratio,actual});
    else if(readerViewMode_==ViewMode::ActualSize)width=actual;
    return std::max(1.0,width*readerZoom_);
}
double App::ReaderHeight() const { return std::max(1.0F, ReaderSize().height - (showInfo_ ? 86.0F : 0.0F)); }

void App::ResetReaderSizingReference() {
    readerSizingPage_=folder_.Empty()?SIZE_MAX:folder_.Index();
    readerSourceWidth_=readerSourceHeight_=0;
    if(!folder_.Empty())if(const auto image=cache_.TryGet(folder_.Current().wstring())) {
        readerSourceWidth_=image->width;readerSourceHeight_=image->height;
    }
}

void App::SetContinuous(const bool enabled) {
    CancelRegionCopy();
    if (continuous_ == enabled) { if (enabled) RefreshContinuous(); return; }
    if (enabled && (hdrEnabled_ || enhancementEnabled_)) {
        if (hdrEnabled_) DisableHdrMode();
        enhancementEnabled_ = false; enhancementInProgress_ = false;
        enhancedImage_.reset(); ++enhancementGeneration_;
        SetNotice(IsEnglish() ? L"Continuous reading uses SDR. RTX enhancement is available in single-page mode."
            : L"直式連續閱讀使用 SDR；RTX 增強可在單頁模式使用。", 6000);
    }
    continuous_ = enabled;
    readerTiles_.clear(); bitmap_.Reset();
    requestedFirst_ = requestedLast_ = SIZE_MAX;
    ShowScrollBar(window_, SB_VERT, enabled);
    if (enabled) {
        readerZoom_ = 1; readerPanX_ = 0;
        ResetReaderSizingReference();
        if (!folder_.Empty()) scrollPosition_ = layout_.Top(folder_.Index());
        RefreshContinuous();
    } else {
        zoomFactor_ = 1; pan_ = {0, 0};
        ScheduleNeighborhood(); ApplyCurrentIfReady(); RefreshAiRequests(); UploadCurrentBitmap();
    }
    UpdateMenuChecks();
    InvalidateRect(window_, nullptr, FALSE);
}

void App::RefreshContinuous() {
    if (!continuous_ || folder_.Empty()) { UpdateScrollBar(); return; }
    if(readerSizingPage_>=folder_.Size())ResetReaderSizingReference();
    if(const auto reference=cache_.TryGet(folder_.At(readerSizingPage_).wstring())) {
        readerSourceWidth_=reference->width;readerSourceHeight_=reference->height;
    }
    const double width = ReaderWidth();
    const double viewport = ReaderHeight() / width;
    // Preserve the leading page and fraction while unknown page sizes resolve.
    const auto anchor = layout_.PageAt(scrollPosition_);
    const double fraction = (scrollPosition_ - layout_.Top(anchor)) / layout_.Height(anchor);
    auto first = anchor;
    auto last = layout_.PageAt(scrollPosition_ + viewport * 2);
    first = first > 2 ? first - 2 : 0;
    last = std::min(folder_.Size() - 1, last + 2);
    last = std::min(last, first + 191);
    for (auto i = first; i <= last; ++i) {
        if (const auto image = cache_.TryGet(folder_.At(i).wstring())) layout_.SetSize(i, image->width, image->height);
    }
    layout_.Rebuild();
    scrollPosition_ = layout_.Top(anchor) + fraction * layout_.Height(anchor);
    scrollPosition_ = std::clamp(scrollPosition_, 0.0, std::max(0.0, layout_.Total() - viewport));
    const auto current = layout_.PageAt(scrollPosition_ + 1e-8);
    if (folder_.MoveTo(current)) {
        currentImage_.reset(); enhancedImage_.reset();
        ++enhancementGeneration_;
        UpdateWindowTitle();
    }
    if (auto image = cache_.TryGet(folder_.Current().wstring())) currentImage_ = std::move(image);
    loading_ = !currentImage_;
    // Anchor the bounded queue at the leading page, so a small zoom cannot
    // spend its entire request budget on pages behind the visible viewport.
    first = current > 2 ? current - 2 : 0;
    const auto visibleLast=std::min(layout_.PageAt(scrollPosition_+viewport),current+127);
    last = std::min(folder_.Size()-1,visibleLast+2);
    if (first != requestedFirst_ || last != requestedLast_) {
        cache_.ClearQueued();
        requestedFirst_ = first; requestedLast_ = last;
        std::vector<std::wstring> pinned;
        // Only visible pages are pinned, not the entire prefetch neighborhood.
        for (auto i = current; i <= visibleLast; ++i) pinned.push_back(folder_.At(i).wstring());
        cache_.Pin(pinned);
        cache_.Request(folder_.Current().wstring(), 1000);
        for (auto i = first; i <= last; ++i) {
            cache_.Request(folder_.At(i).wstring(),i>=current?950-static_cast<int>(i-current):800-static_cast<int>(current-i));
        }
    }
    RefreshAiRequests();
    UpdateScrollBar();
    InvalidateRect(window_, nullptr, FALSE);
}

void App::ScrollContinuous(const double delta) {
    if (folder_.Empty()) return;
    scrollPosition_ = std::clamp(scrollPosition_ + delta / ReaderWidth(),
        0.0, std::max(0.0, layout_.Total() - ReaderHeight() / ReaderWidth()));
    RefreshContinuous();
}
void App::JumpContinuous(const std::size_t index, const bool bottom) {
    if (index >= folder_.Size()) return;
    scrollPosition_ = bottom ? layout_.Total() : layout_.Top(index);
    RefreshContinuous();
}
void App::UpdateScrollBar() {
    if (!continuous_) return;
    constexpr int range = 1000000;
    const double total = layout_.Total();
    const double viewport = ReaderHeight() / ReaderWidth();
    SCROLLINFO info{sizeof(info), SIF_RANGE | SIF_PAGE | SIF_POS | SIF_DISABLENOSCROLL};
    info.nMin = 0; info.nMax = range;
    info.nPage = total > 0 ? static_cast<UINT>(std::clamp(viewport / total, 0.0, 1.0) * (range + 1)) : range + 1;
    info.nPos = total > 0 ? static_cast<int>(scrollPosition_ / total * (range + 1)) : 0;
    SetScrollInfo(window_, SB_VERT, &info, TRUE);
}

void App::DrawContinuous(const D2D1_SIZE_F& size) {
    DrawContinuousInto(renderTarget_.Get(),size);
}
void App::DrawContinuousInto(ID2D1RenderTarget* target,const D2D1_SIZE_F& size) {
    if (folder_.Empty()) return;
    const float top = showInfo_ ? 48.0F : 0.0F;
    const float bottom = size.height - (showInfo_ ? 38.0F : 0.0F);
    const double width = ReaderWidth();
    const float panLimit = static_cast<float>(std::max(0.0, (width - size.width) / 2));
    readerPanX_ = std::clamp(readerPanX_, -panLimit, panLimit);
    const float left = static_cast<float>((size.width - width) / 2 + readerPanX_);
    target->PushAxisAlignedClip(D2D1::RectF(0, top, size.width, bottom), D2D1_ANTIALIAS_MODE_ALIASED);
    const auto first = layout_.PageAt(scrollPosition_);
    const auto last = layout_.PageAt(scrollPosition_ + (bottom - top) / width);
    std::erase_if(readerTiles_, [&](const auto& entry) {
        return std::get<0>(entry.first) < first || std::get<0>(entry.first) > last;
    });
    std::erase_if(readerTileSources_,[&](const auto& entry){return entry.first<first || entry.first>last;});
    std::erase_if(readerTileBorders_,[&](const auto& entry){return entry.first<first || entry.first>last;});
    constexpr std::size_t maximumTileBytes = 96ULL * 1024 * 1024;
    std::size_t tileBytes = 0;
    for (const auto& [key, tile] : readerTiles_) {
        (void)key;
        const auto extent = tile->GetPixelSize(); tileBytes += static_cast<std::size_t>(extent.width) * extent.height * 4;
    }
    std::set<std::tuple<std::size_t, unsigned, unsigned>> retained;
    for (auto i = first; i <= last && i < first + 128; ++i) {
        const float y = static_cast<float>(top + (layout_.Top(i) - scrollPosition_) * width);
        const float endY = static_cast<float>(top + (layout_.Top(i + 1) - scrollPosition_) * width);
        const auto original = i == folder_.Index() && currentImage_ ? currentImage_ : cache_.TryGet(folder_.At(i).wstring());
        const auto image=AiDisplayImage(i,original);
        const double scale=image?width/image->width:1;
        const unsigned maximumBitmap=target->GetMaximumBitmapSize();
        const auto border=std::min(maximumBitmap/4,static_cast<unsigned>(std::clamp(std::ceil(1.0/scale),1.0,512.0)));
        if(readerTileSources_[i].lock()!=image || readerTileBorders_[i]!=border) {
            std::erase_if(readerTiles_,[&](const auto& entry) {
                if(std::get<0>(entry.first)!=i)return false;
                const auto extent=entry.second->GetPixelSize();
                tileBytes-=static_cast<std::size_t>(extent.width)*extent.height*4;
                return true;
            });
            readerTileSources_[i]=image;
            readerTileBorders_[i]=border;
        }
        if (!image) {
            const auto error = cache_.ErrorFor(folder_.At(i).wstring());
            std::wstring label = std::to_wstring(i + 1) + (error.empty()
                ? (IsEnglish() ? L" · Loading…" : L" · 載入中…")
                : (IsEnglish() ? L" · Could not load image (reopen to retry): " : L" · 圖片載入失敗（重新開啟可重試）：")+error);
            const auto bounds=D2D1::RectF(std::max(12.0F,left+12),std::max(top,y)+16,size.width-12,std::min(bottom,endY));
            Microsoft::WRL::ComPtr<IDWriteTextLayout> labelLayout;
            if(!error.empty() && SUCCEEDED(writeFactory_->CreateTextLayout(label.c_str(),static_cast<UINT32>(label.size()),
                textFormat_.Get(),std::max(1.0F,bounds.right-bounds.left),std::max(1.0F,bounds.bottom-bounds.top),&labelLayout))) {
                labelLayout->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
                target->DrawTextLayout(D2D1::Point2F(bounds.left,bounds.top),labelLayout.Get(),mutedBrush_.Get(),D2D1_DRAW_TEXT_OPTIONS_CLIP);
            } else target->DrawTextW(label.c_str(),static_cast<UINT32>(label.size()),textFormat_.Get(),bounds,mutedBrush_.Get());
            continue;
        }
        if(maximumBitmap<3)continue;
        const unsigned tileSide = std::min(2048U, maximumBitmap-2*border);
        const unsigned startRow = static_cast<unsigned>(std::max(0.0, (top - y) / scale)) / tileSide * tileSide;
        const unsigned startCol = static_cast<unsigned>(std::max(0.0, -left / scale)) / tileSide * tileSide;
        for (unsigned row = startRow; row < image->height && y + row * scale < bottom; row += tileSide) {
            for (unsigned col = startCol; col < image->width && left + col * scale < size.width; col += tileSide) {
                const auto key = std::tuple{i, col, row};
                Microsoft::WRL::ComPtr<ID2D1Bitmap> tile;
                if (auto existing = readerTiles_.find(key); existing != readerTiles_.end()) tile = existing->second;
                const unsigned w = std::min(tileSide, image->width - col);
                const unsigned h = std::min(tileSide, image->height - row);
                const unsigned sourceLeft=col>border?col-border:0,sourceTop=row>border?row-border:0;
                const unsigned sourceRight=std::min(image->width,col+w+border),sourceBottom=std::min(image->height,row+h+border);
                const unsigned paddedWidth=sourceRight-sourceLeft,paddedHeight=sourceBottom-sourceTop;
                if (!tile) {
                    const auto properties = D2D1::BitmapProperties(D2D1::PixelFormat(
                        DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96);
                    target->CreateBitmap(D2D1::SizeU(paddedWidth, paddedHeight),
                        image->pixels.data() + static_cast<std::size_t>(sourceTop) * image->stride + sourceLeft * 4ULL,
                        image->stride, properties, &tile);
                    const auto bytes = static_cast<std::size_t>(paddedWidth) * paddedHeight * 4;
                    if (tile && tileBytes + bytes <= maximumTileBytes) {
                        readerTiles_.emplace(key, tile); tileBytes += bytes;
                    }
                }
                if (tile) {
                    retained.insert(key);
                    // Sample neighboring pixels across texture edges, then clip
                    // each core to a shared aliased boundary. Independent filtered
                    // quads otherwise fade into the background at fractional zoom.
                    target->PushAxisAlignedClip(D2D1::RectF(
                        col?static_cast<float>(left + col * scale):0,
                        row?static_cast<float>(y + row * scale):top,
                        col+w<image->width?static_cast<float>(left + (col + w) * scale):size.width,
                        row+h<image->height?static_cast<float>(y + (row + h) * scale):bottom),
                        D2D1_ANTIALIAS_MODE_ALIASED);
                    target->DrawBitmap(tile.Get(), D2D1::RectF(
                        static_cast<float>(left + sourceLeft * scale), static_cast<float>(y + sourceTop * scale),
                        static_cast<float>(left + sourceRight * scale), static_cast<float>(y + sourceBottom * scale)),
                        1, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
                    target->PopAxisAlignedClip();
                    // Uncached tiles are consumed before releasing their GPU memory.
                    if (!readerTiles_.contains(key)) target->Flush();
                }
            }
        }
    }
    std::erase_if(readerTiles_, [&](const auto& entry) { return !retained.contains(entry.first); });
    if(last>=first+128) {
        const auto* label=IsEnglish()?L"Zoom in to display more pages.":L"請放大畫面以顯示更多頁面。";
        target->DrawTextW(label,static_cast<UINT32>(wcslen(label)),textFormat_.Get(),
            D2D1::RectF(12,std::max(top,bottom-30),size.width-12,bottom),mutedBrush_.Get());
    }
    target->PopAxisAlignedClip();
}
