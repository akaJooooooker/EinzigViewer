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
    cache_.SetSourceLease(folder_.Book());
    currentImage_.reset(); enhancedImage_.reset(); bitmap_.Reset(); readerTiles_.clear();
    showOriginalForComparison_ = false;
    ++enhancementGeneration_;
    zoomFactor_ = 1; readerZoom_ = 1; readerPanX_ = 0;
    pan_ = {0, 0}; loading_ = true;
    layout_.Reset(folder_.Size()); layout_.Rebuild();
    scrollPosition_ = folder_.Empty() ? 0 : layout_.Top(folder_.Index());
    requestedFirst_ = requestedLast_ = SIZE_MAX;
    if (continuous_) RefreshContinuous();
    else { ScheduleNeighborhood(); ApplyCurrentIfReady(); }
    UpdateWindowTitle();
    InvalidateRect(window_, nullptr, FALSE);
}

D2D1_SIZE_F App::ReaderSize() const {
    RECT rect{}; GetClientRect(window_, &rect);
    const float dpi = static_cast<float>(GetDpiForWindow(window_)) / 96.0F;
    return D2D1::SizeF(std::max(1.0F, rect.right / dpi), std::max(1.0F, rect.bottom / dpi));
}
double App::ReaderWidth() const { return std::max(1.0, static_cast<double>(ReaderSize().width - 24) * readerZoom_); }
double App::ReaderHeight() const { return std::max(1.0F, ReaderSize().height - (showInfo_ ? 86.0F : 0.0F)); }

void App::SetContinuous(const bool enabled) {
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
        if (!folder_.Empty()) scrollPosition_ = layout_.Top(folder_.Index());
        RefreshContinuous();
    } else {
        zoomFactor_ = 1; pan_ = {0, 0};
        ScheduleNeighborhood(); ApplyCurrentIfReady(); UploadCurrentBitmap();
    }
    UpdateMenuChecks();
    InvalidateRect(window_, nullptr, FALSE);
}

void App::RefreshContinuous() {
    if (!continuous_ || folder_.Empty()) { UpdateScrollBar(); return; }
    const double width = ReaderWidth();
    const double viewport = ReaderHeight() / width;
    // Preserve the leading page and fraction while unknown page sizes resolve.
    const auto anchor = layout_.PageAt(scrollPosition_);
    const double fraction = (scrollPosition_ - layout_.Top(anchor)) / layout_.Height(anchor);
    auto first = layout_.PageAt(std::max(0.0, scrollPosition_ - viewport));
    auto last = layout_.PageAt(scrollPosition_ + viewport * 2);
    first = first > 2 ? first - 2 : 0;
    last = std::min(folder_.Size() - 1, last + 2);
    last = std::min(last, first + 63);
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
    first = layout_.PageAt(std::max(0.0, scrollPosition_ - viewport));
    last = layout_.PageAt(scrollPosition_ + viewport * 2);
    first = first > 2 ? first - 2 : 0;
    last = std::min({folder_.Size() - 1, last + 2, first + 63});
    if (first != requestedFirst_ || last != requestedLast_) {
        cache_.ClearQueued();
        requestedFirst_ = first; requestedLast_ = last;
        std::vector<std::wstring> pinned;
        // Only visible pages are pinned, not the entire prefetch neighborhood.
        const auto visibleLast = layout_.PageAt(scrollPosition_ + viewport);
        for (auto i = current; i <= std::min(visibleLast, current + 63); ++i) pinned.push_back(folder_.At(i).wstring());
        cache_.Pin(pinned);
        cache_.Request(folder_.Current().wstring(), 1000);
        for (auto i = first; i <= last; ++i) {
            cache_.Request(folder_.At(i).wstring(), 900 - static_cast<int>(i > current ? i - current : current - i));
        }
    }
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
    if (folder_.Empty()) return;
    const float top = showInfo_ ? 48.0F : 0.0F;
    const float bottom = size.height - (showInfo_ ? 38.0F : 0.0F);
    const double width = ReaderWidth();
    const float panLimit = static_cast<float>(std::max(0.0, (width - size.width) / 2));
    readerPanX_ = std::clamp(readerPanX_, -panLimit, panLimit);
    const float left = static_cast<float>((size.width - width) / 2 + readerPanX_);
    renderTarget_->PushAxisAlignedClip(D2D1::RectF(0, top, size.width, bottom), D2D1_ANTIALIAS_MODE_ALIASED);
    const auto first = layout_.PageAt(scrollPosition_);
    const auto last = layout_.PageAt(scrollPosition_ + (bottom - top) / width);
    std::erase_if(readerTiles_, [&](const auto& entry) {
        return std::get<0>(entry.first) < first || std::get<0>(entry.first) > last;
    });
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
        const auto image = i == folder_.Index() && currentImage_ ? currentImage_ : cache_.TryGet(folder_.At(i).wstring());
        if (!image) {
            const auto error = cache_.ErrorFor(folder_.At(i).wstring());
            std::wstring label = std::to_wstring(i + 1) + (error.empty()
                ? (IsEnglish() ? L" · Loading…" : L" · 載入中…")
                : (IsEnglish() ? L" · Could not decode image (reopen to retry)" : L" · 無法解碼圖片（重新開啟可重試）"));
            renderTarget_->DrawTextW(label.c_str(), static_cast<UINT32>(label.size()), textFormat_.Get(),
                D2D1::RectF(std::max(12.0F, left + 12), std::max(top, y) + 16, size.width - 12, std::min(bottom, endY)), mutedBrush_.Get());
            continue;
        }
        const double scale = width / image->width;
        const unsigned tileSide = std::min(2048U, renderTarget_->GetMaximumBitmapSize());
        const unsigned startRow = static_cast<unsigned>(std::max(0.0, (top - y) / scale)) / tileSide * tileSide;
        const unsigned startCol = static_cast<unsigned>(std::max(0.0, -left / scale)) / tileSide * tileSide;
        for (unsigned row = startRow; row < image->height && y + row * scale < bottom; row += tileSide) {
            for (unsigned col = startCol; col < image->width && left + col * scale < size.width; col += tileSide) {
                const auto key = std::tuple{i, col, row};
                Microsoft::WRL::ComPtr<ID2D1Bitmap> tile;
                if (auto existing = readerTiles_.find(key); existing != readerTiles_.end()) tile = existing->second;
                const unsigned w = std::min(tileSide, image->width - col);
                const unsigned h = std::min(tileSide, image->height - row);
                if (!tile) {
                    const auto properties = D2D1::BitmapProperties(D2D1::PixelFormat(
                        DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96);
                    renderTarget_->CreateBitmap(D2D1::SizeU(w, h),
                        image->pixels.data() + static_cast<std::size_t>(row) * image->stride + col * 4ULL,
                        image->stride, properties, &tile);
                    const auto bytes = static_cast<std::size_t>(w) * h * 4;
                    if (tile && tileBytes + bytes <= maximumTileBytes) {
                        readerTiles_.emplace(key, tile); tileBytes += bytes;
                    }
                }
                if (tile) {
                    retained.insert(key);
                    renderTarget_->DrawBitmap(tile.Get(), D2D1::RectF(
                        static_cast<float>(left + col * scale), static_cast<float>(y + row * scale),
                        static_cast<float>(left + (col + w) * scale), static_cast<float>(y + (row + h) * scale)),
                        1, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
                    // Uncached tiles are consumed before releasing their GPU memory.
                    if (!readerTiles_.contains(key)) renderTarget_->Flush();
                }
            }
        }
    }
    std::erase_if(readerTiles_, [&](const auto& entry) { return !retained.contains(entry.first); });
    renderTarget_->PopAxisAlignedClip();
}
