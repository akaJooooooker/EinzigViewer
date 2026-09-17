#pragma once

#include "FolderModel.h"
#include "EnhancementWorker.h"
#include "ImageCache.h"
#include "ImageEnhancer.h"
#include "ContinuousLayout.h"
#if EINZIGVIEWER_WITH_RTX
#include "RtxHdrPresenter.h"
#endif

#include <Windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <filesystem>
#include <atomic>
#include <memory>
#include <string>
#include <future>
#include <map>
#include <tuple>

enum class ViewMode : int {
    FitWindow = 0,
    FitWidth = 1,
    ActualSize = 2
};

enum class WheelBehavior : int {
    Navigate = 0,
    Zoom = 1
};

enum class UiLanguage : int {
    TraditionalChinese = 0,
    English = 1
};

class App {
#ifdef EINZIGVIEWER_TESTING
    friend struct ReaderUiTest;
#endif
public:
    App();
    ~App();
    int Run(HINSTANCE instance, const std::filesystem::path& initialFile);

private:
    static LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK HdrSurfaceProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);

    bool CreateMainWindow(HINSTANCE instance);
    HMENU CreateApplicationMenu();
    void UpdateMenuChecks();
    void SetLanguage(UiLanguage language);
    [[nodiscard]] bool IsEnglish() const noexcept { return language_ == UiLanguage::English; }
    void ShowRtxError(const std::wstring& details = {});
    bool CreateDeviceIndependentResources();
    bool EnsureRenderTarget();
    void DiscardRenderTarget();
    void Paint();
    void DrawOverlay(const D2D1_SIZE_F& size);

    void ShowOpenDialog();
    void StartBookOpen(const std::filesystem::path& file);
    void PollBookOpen();
    void ResetDocument();
    void SetContinuous(bool enabled);
    void DrawContinuous(const D2D1_SIZE_F& size);
    void RefreshContinuous();
    void ScrollContinuous(double delta);
    void JumpContinuous(std::size_t index, bool bottom = false);
    void UpdateScrollBar();
    [[nodiscard]] D2D1_SIZE_F ReaderSize() const;
    [[nodiscard]] double ReaderWidth() const;
    [[nodiscard]] double ReaderHeight() const;
    void ShowExportPngDialog(bool resize);
    bool OpenImage(const std::filesystem::path& file);
    void Navigate(int delta);
    void NavigateTo(std::size_t index);
    void ScheduleNeighborhood();
    void ApplyCurrentIfReady();
    void RequestEnhancement();
    void ApplyEnhancementResult();
    void ToggleEnhancement();
    void ToggleHdrMode();
    void DisableHdrMode(bool restoreSdrBackend = true);
    void SetHdrPreset(int preset);
    bool PaintHdr(std::wstring& error);
    bool CreateHdrSurface(std::wstring& error);
    void DestroyHdrSurface() noexcept;
    void UpdateHdrSurfaceBounds();
    void RecreateEnhancementBackend();
    void ClampHdrZoom();
    void UploadCurrentBitmap();
    void SetViewMode(ViewMode mode);
    void SetWheelBehavior(WheelBehavior behavior);
    void ZoomAt(float factor, POINT clientPoint);
    [[nodiscard]] float BaseScale(const D2D1_SIZE_F& clientSize) const;
    [[nodiscard]] float CurrentScale(const D2D1_SIZE_F& clientSize) const;
    [[nodiscard]] D2D1_RECT_F ImageRectangle(const D2D1_SIZE_F& clientSize) const;

    void ToggleMaximized();
    void ToggleFullscreen();
    void ArmFullscreenCursorHide();
    void ShowFullscreenCursor() noexcept;
    void HideFullscreenCursorIfEligible();
    void ShowContextMenu(POINT screenPoint);
    void SetNotice(std::wstring text, DWORD milliseconds = 3500);
    void UpdateWindowTitle();
    void LoadSettings();
    void SaveSettings() const;
    [[nodiscard]] std::filesystem::path SettingsPath() const;

    HWND window_ = nullptr;
    HWND hdrSurfaceWindow_ = nullptr;
    HMENU mainMenu_ = nullptr;
    HMENU viewMenu_ = nullptr;
    HMENU wheelMenu_ = nullptr;
    HMENU rtxMenu_ = nullptr;
    HMENU hdrPresetMenu_ = nullptr;
    HMENU languageMenu_ = nullptr;
    FolderModel folder_;
    ImageCache cache_;
    std::unique_ptr<ImageEnhancer> enhancer_;
    std::unique_ptr<EnhancementWorker> enhancementWorker_;
#if EINZIGVIEWER_WITH_RTX
    std::unique_ptr<RtxHdrPresenter> hdrPresenter_;
    std::shared_ptr<std::atomic_bool> hdrRetirement_;
#endif
    std::shared_ptr<ImageData> currentImage_;
    std::shared_ptr<ImageData> enhancedImage_;

    Microsoft::WRL::ComPtr<ID2D1Factory> d2dFactory_;
    Microsoft::WRL::ComPtr<IDWriteFactory> writeFactory_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> textFormat_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> titleFormat_;
    Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> renderTarget_;
    Microsoft::WRL::ComPtr<ID2D1Bitmap> bitmap_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> textBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> mutedBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> accentBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> overlayBrush_;

    ViewMode viewMode_ = ViewMode::FitWindow;
    WheelBehavior wheelBehavior_ = WheelBehavior::Navigate;
    UiLanguage language_ = UiLanguage::TraditionalChinese;
    float zoomFactor_ = 1.0F;
    D2D1_POINT_2F pan_{0.0F, 0.0F};
    POINT dragOrigin_{};
    D2D1_POINT_2F panOrigin_{};
    bool dragging_ = false;
    bool showInfo_ = true;
    bool loading_ = false;
    bool enhancementEnabled_ = false;
    bool enhancementInProgress_ = false;
    bool showOriginalForComparison_ = false;
    bool hdrEnabled_ = false;
    bool hdrLastUsedVsr_ = false;
    int hdrPreset_ = 0;
    std::uint64_t enhancementGeneration_ = 0;
    bool fullscreen_ = false;
    bool fullscreenCursorHidden_ = false;
    unsigned int fullscreenCursorHideCalls_ = 0;
    WINDOWPLACEMENT windowPlacement_{sizeof(WINDOWPLACEMENT)};
    DWORD windowStyle_ = 0;
    std::wstring notice_;
    ULONGLONG noticeUntil_ = 0;
    bool continuous_ = false;
    ContinuousLayout layout_;
    double scrollPosition_ = 0;
    double dragScrollOrigin_ = 0;
    int wheelRemainder_ = 0;
    float readerZoom_ = 1.0F;
    float readerPanX_ = 0;
    std::size_t requestedFirst_ = SIZE_MAX;
    std::size_t requestedLast_ = SIZE_MAX;
    std::map<std::tuple<std::size_t, unsigned, unsigned>, Microsoft::WRL::ComPtr<ID2D1Bitmap>> readerTiles_;
    struct BookOpenResult { std::shared_ptr<EpubBook> book; std::wstring error; };
    std::future<BookOpenResult> bookFuture_;
    std::shared_ptr<std::atomic_bool> bookCancelled_;
    std::filesystem::path pendingBook_;
};

