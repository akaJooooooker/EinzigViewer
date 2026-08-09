#include "App.h"

#include "NullImageEnhancer.h"
#if EIDOLYX_WITH_RTX
#include "RtxImageEnhancer.h"
#endif

#include <Windowsx.h>
#include <d2d1helper.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <algorithm>
#include <cmath>
#include <exception>
#include <sstream>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {
constexpr wchar_t WindowClassName[] = L"Eidolyx.MainWindow";
constexpr wchar_t HdrSurfaceClassName[] = L"Eidolyx.HdrSurface";
constexpr wchar_t ProductName[] = L"Eidolyx";
constexpr WORD AppIconResource = 101;
constexpr UINT_PTR NoticeTimer = 1;
constexpr UINT_PTR EnhancementResizeTimer = 2;
constexpr UINT_PTR HdrRetirementTimer = 3;
constexpr UINT_PTR FullscreenCursorTimer = 4;
constexpr UINT FullscreenCursorDelayMilliseconds = 3000;
constexpr int LayoutSettingsVersion = 4;

enum Command : UINT {
    CommandOpen = 100,
    CommandPrevious,
    CommandNext,
    CommandFit,
    CommandFitWidth,
    CommandActual,
    CommandMaximize,
    CommandFullscreen,
    CommandToggleInfo,
    CommandRtx,
    CommandHdr,
    CommandHdrStandard,
    CommandHdrVivid,
    CommandHdrGentle,
    CommandWheelNavigate,
    CommandWheelZoom,
    CommandFirst,
    CommandLast,
    CommandAbout,
    CommandLanguageChinese,
    CommandLanguageEnglish,
    CommandExit
};

const wchar_t* UiText(
    const bool english, const wchar_t* traditionalChinese, const wchar_t* englishText) {
    return english ? englishText : traditionalChinese;
}

std::wstring ViewModeName(const ViewMode mode, const bool english) {
    switch (mode) {
    case ViewMode::FitWidth: return UiText(english, L"適合寬度", L"Fit width");
    case ViewMode::ActualSize: return UiText(english, L"原始大小", L"Actual size");
    default: return UiText(english, L"適合視窗", L"Fit window");
    }
}

D2D1_RECT_F ClientRectF(const HWND window) {
    RECT rectangle{};
    GetClientRect(window, &rectangle);
    return D2D1::RectF(0.0F, 0.0F, static_cast<float>(rectangle.right), static_cast<float>(rectangle.bottom));
}
}

App::App() : cache_(768ULL * 1024ULL * 1024ULL) {
#if EIDOLYX_WITH_RTX
    enhancer_ = std::make_unique<RtxImageEnhancer>();
#else
    enhancer_ = std::make_unique<NullImageEnhancer>();
#endif
}

App::~App() = default;

int App::Run(const HINSTANCE instance, const std::filesystem::path& initialFile) {
    if (!CreateDeviceIndependentResources() || !CreateMainWindow(instance)) {
        MessageBoxW(nullptr,
            L"無法初始化 Eidolyx。\n\nEidolyx could not be initialized.",
            ProductName, MB_OK | MB_ICONERROR);
        return 1;
    }

    LoadSettings();
    mainMenu_ = CreateApplicationMenu();
    if (mainMenu_) SetMenu(window_, mainMenu_);
    UpdateMenuChecks();
    ShowWindow(window_, SW_SHOW);
    UpdateWindow(window_);

    if (!initialFile.empty()) OpenImage(initialFile);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}

bool App::CreateMainWindow(const HINSTANCE instance) {
    WNDCLASSEXW windowClass{sizeof(WNDCLASSEXW)};
    windowClass.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    windowClass.lpfnWndProc = WindowProcedure;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(AppIconResource));
    windowClass.hIconSm = static_cast<HICON>(LoadImageW(
        instance, MAKEINTRESOURCEW(AppIconResource), IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
    if (!windowClass.hIcon) windowClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    if (!windowClass.hIconSm) windowClass.hIconSm = windowClass.hIcon;
    windowClass.lpszClassName = WindowClassName;
    if (!RegisterClassExW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;

    POINT cursor{};
    GetCursorPos(&cursor);
    MONITORINFO monitor{sizeof(MONITORINFO)};
    GetMonitorInfoW(MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY), &monitor);
    constexpr int initialWidth = 960;
    constexpr int initialHeight = 640;
    const int initialX = monitor.rcWork.left +
        (monitor.rcWork.right - monitor.rcWork.left - initialWidth) / 2;
    const int initialY = monitor.rcWork.top +
        (monitor.rcWork.bottom - monitor.rcWork.top - initialHeight) / 2;
    window_ = CreateWindowExW(
        WS_EX_ACCEPTFILES, WindowClassName, ProductName, WS_OVERLAPPEDWINDOW,
        initialX, initialY, initialWidth, initialHeight,
        nullptr, nullptr, instance, this);
    if (!window_) return false;
    DragAcceptFiles(window_, TRUE);
    cache_.SetNotificationWindow(window_);
    enhancementWorker_ = std::make_unique<EnhancementWorker>(*enhancer_, window_);
    return true;
}

bool App::CreateHdrSurface(std::wstring& error) {
    if (hdrSurfaceWindow_ && IsWindow(hdrSurfaceWindow_)) return true;
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW windowClass{sizeof(WNDCLASSEXW)};
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = HdrSurfaceProcedure;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    windowClass.lpszClassName = HdrSurfaceClassName;
    if (!RegisterClassExW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        error = L"無法註冊主視窗 HDR 顯示區。 / Could not register the main-window HDR surface.";
        return false;
    }

    hdrSurfaceWindow_ = CreateWindowExW(
        WS_EX_NOACTIVATE, HdrSurfaceClassName, nullptr,
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
        0, 0, 1, 1, window_, nullptr, instance, nullptr);
    if (!hdrSurfaceWindow_) {
        error = L"無法在 Eidolyx 主視窗建立 HDR 顯示區。 / "
                L"Could not create the HDR surface inside the Eidolyx main window.";
        return false;
    }
    UpdateHdrSurfaceBounds();
    return true;
}

void App::DestroyHdrSurface() noexcept {
    if (hdrSurfaceWindow_ && IsWindow(hdrSurfaceWindow_)) DestroyWindow(hdrSurfaceWindow_);
    hdrSurfaceWindow_ = nullptr;
}

void App::UpdateHdrSurfaceBounds() {
    if (!hdrSurfaceWindow_ || !IsWindow(hdrSurfaceWindow_)) return;
    RECT client{};
    GetClientRect(window_, &client);
    const float dpiScale = static_cast<float>(GetDpiForWindow(window_)) / 96.0F;
    const int top = showInfo_ ? static_cast<int>(std::lround(48.0F * dpiScale)) : 0;
    const int bottom = showInfo_ ? static_cast<int>(std::lround(38.0F * dpiScale)) : 0;
    const int width = std::max(1L, client.right - client.left);
    const int height = std::max(1L, client.bottom - client.top - top - bottom);
    SetWindowPos(hdrSurfaceWindow_, HWND_TOP, 0, top, width, height,
        SWP_NOACTIVATE | SWP_SHOWWINDOW);
#if EIDOLYX_WITH_RTX
    if (hdrPresenter_) hdrPresenter_->Invalidate();
#endif
}

HMENU App::CreateApplicationMenu() {
    const bool english = IsEnglish();
    HMENU root = CreateMenu();
    HMENU fileMenu = CreatePopupMenu();
    HMENU navigateMenu = CreatePopupMenu();
    viewMenu_ = CreatePopupMenu();
    wheelMenu_ = CreatePopupMenu();
    rtxMenu_ = CreatePopupMenu();
    hdrPresetMenu_ = CreatePopupMenu();
    HMENU helpMenu = CreatePopupMenu();
    languageMenu_ = CreatePopupMenu();
    if (!root || !fileMenu || !navigateMenu || !viewMenu_ || !wheelMenu_ ||
        !rtxMenu_ || !hdrPresetMenu_ || !helpMenu || !languageMenu_) return root;

    AppendMenuW(fileMenu, MF_STRING, CommandOpen,
        UiText(english, L"開啟圖片…\tO", L"Open image…\tO"));
    AppendMenuW(fileMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(fileMenu, MF_STRING, CommandExit,
        UiText(english, L"結束\tAlt+F4", L"Exit\tAlt+F4"));

    AppendMenuW(navigateMenu, MF_STRING, CommandPrevious,
        UiText(english, L"上一張\t←", L"Previous\t←"));
    AppendMenuW(navigateMenu, MF_STRING, CommandNext,
        UiText(english, L"下一張\t→", L"Next\t→"));
    AppendMenuW(navigateMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(navigateMenu, MF_STRING, CommandFirst,
        UiText(english, L"第一張\tHome", L"First\tHome"));
    AppendMenuW(navigateMenu, MF_STRING, CommandLast,
        UiText(english, L"最後一張\tEnd", L"Last\tEnd"));

    AppendMenuW(viewMenu_, MF_STRING, CommandFit,
        UiText(english, L"適合視窗\t1", L"Fit window\t1"));
    AppendMenuW(viewMenu_, MF_STRING, CommandFitWidth,
        UiText(english, L"適合寬度\t2", L"Fit width\t2"));
    AppendMenuW(viewMenu_, MF_STRING, CommandActual,
        UiText(english, L"原始大小（100%）\t3", L"Actual size (100%)\t3"));
    AppendMenuW(viewMenu_, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(viewMenu_, MF_STRING, CommandMaximize,
        UiText(english, L"視窗最大化\tM", L"Maximize window\tM"));
    AppendMenuW(viewMenu_, MF_STRING, CommandFullscreen,
        UiText(english, L"無邊框全螢幕\tF11", L"Borderless fullscreen\tF11"));
    AppendMenuW(viewMenu_, MF_STRING, CommandToggleInfo,
        UiText(english, L"顯示資訊列\tI", L"Show information bar\tI"));
    AppendMenuW(viewMenu_, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(wheelMenu_, MF_STRING, CommandWheelNavigate,
        UiText(english, L"上一張／下一張", L"Previous/Next"));
    AppendMenuW(wheelMenu_, MF_STRING, CommandWheelZoom,
        UiText(english, L"放大／縮小", L"Zoom in/out"));
    AppendMenuW(viewMenu_, MF_POPUP, reinterpret_cast<UINT_PTR>(wheelMenu_),
        UiText(english, L"滑鼠滾輪", L"Mouse wheel"));

    AppendMenuW(rtxMenu_, MF_STRING, CommandRtx,
        UiText(english, L"RTX VSR 超解析度\tR", L"RTX VSR Super Resolution\tR"));
    AppendMenuW(rtxMenu_, MF_STRING, CommandHdr,
        UiText(english, L"RTX 視訊增強(VSR + HDR)\tH", L"RTX Video Enhancement (VSR + HDR)\tH"));
    AppendMenuW(hdrPresetMenu_, MF_STRING, CommandHdrStandard,
        UiText(english, L"標準", L"Standard"));
    AppendMenuW(hdrPresetMenu_, MF_STRING, CommandHdrVivid,
        UiText(english, L"鮮明", L"Vivid"));
    AppendMenuW(hdrPresetMenu_, MF_STRING, CommandHdrGentle,
        UiText(english, L"柔和", L"Gentle"));
    AppendMenuW(rtxMenu_, MF_POPUP, reinterpret_cast<UINT_PTR>(hdrPresetMenu_),
        UiText(english, L"HDR 預設", L"HDR preset"));
    AppendMenuW(rtxMenu_, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(rtxMenu_, MF_STRING | MF_DISABLED, 0,
        UiText(english, L"HDR：請確保 Windows 與螢幕已開啟 HDR",
            L"HDR: Enable HDR in Windows and on the display"));
    AppendMenuW(rtxMenu_, MF_STRING | MF_DISABLED, 0,
        UiText(english, L"品質：Ultra（4）", L"Quality: Ultra (4)"));
    AppendMenuW(rtxMenu_, MF_STRING | MF_DISABLED, 0,
        UiText(english, L"輸出：依顯示尺寸（最高 7680 px）",
            L"Output: Display-sized (7680 px maximum)"));
    AppendMenuW(rtxMenu_, MF_STRING | MF_DISABLED, 0,
        UiText(english, L"HDR：在原本主視窗呈現", L"HDR: Presented in the original main window"));
    AppendMenuW(rtxMenu_, MF_STRING | MF_DISABLED, 0,
        UiText(english, L"按住 C：顯示原圖比較", L"Hold C: Compare original"));

    AppendMenuW(languageMenu_, MF_STRING, CommandLanguageChinese, L"繁體中文");
    AppendMenuW(languageMenu_, MF_STRING, CommandLanguageEnglish, L"English");
    AppendMenuW(helpMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(languageMenu_),
        UiText(english, L"語言", L"Language"));
    AppendMenuW(helpMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(helpMenu, MF_STRING, CommandAbout,
        UiText(english, L"關於 Eidolyx…", L"About Eidolyx…"));

    AppendMenuW(root, MF_POPUP, reinterpret_cast<UINT_PTR>(fileMenu),
        UiText(english, L"檔案(&F)", L"File(&F)"));
    AppendMenuW(root, MF_POPUP, reinterpret_cast<UINT_PTR>(navigateMenu),
        UiText(english, L"瀏覽(&N)", L"Navigate(&N)"));
    AppendMenuW(root, MF_POPUP, reinterpret_cast<UINT_PTR>(viewMenu_),
        UiText(english, L"檢視(&V)", L"View(&V)"));
    AppendMenuW(root, MF_POPUP, reinterpret_cast<UINT_PTR>(rtxMenu_), L"RTX(&R)");
    AppendMenuW(root, MF_POPUP, reinterpret_cast<UINT_PTR>(helpMenu),
        UiText(english, L"說明(&H)", L"Help(&H)"));
    UpdateMenuChecks();
    return root;
}

void App::UpdateMenuChecks() {
    if (viewMenu_) {
        CheckMenuRadioItem(viewMenu_, CommandFit, CommandActual,
            viewMode_ == ViewMode::FitWindow ? CommandFit :
            viewMode_ == ViewMode::FitWidth ? CommandFitWidth : CommandActual,
            MF_BYCOMMAND);
        CheckMenuItem(viewMenu_, CommandFullscreen,
            MF_BYCOMMAND | (fullscreen_ ? MF_CHECKED : MF_UNCHECKED));
        CheckMenuItem(viewMenu_, CommandMaximize,
            MF_BYCOMMAND | (IsZoomed(window_) && !fullscreen_ ? MF_CHECKED : MF_UNCHECKED));
        CheckMenuItem(viewMenu_, CommandToggleInfo,
            MF_BYCOMMAND | (showInfo_ ? MF_CHECKED : MF_UNCHECKED));
    }
    if (wheelMenu_) {
        CheckMenuRadioItem(wheelMenu_, CommandWheelNavigate, CommandWheelZoom,
            wheelBehavior_ == WheelBehavior::Navigate ? CommandWheelNavigate : CommandWheelZoom,
            MF_BYCOMMAND);
    }
    if (rtxMenu_) {
        CheckMenuItem(rtxMenu_, CommandRtx,
            MF_BYCOMMAND | (enhancementEnabled_ ? MF_CHECKED : MF_UNCHECKED));
        CheckMenuItem(rtxMenu_, CommandHdr,
            MF_BYCOMMAND | (hdrEnabled_ ? MF_CHECKED : MF_UNCHECKED));
    }
    if (hdrPresetMenu_) {
        CheckMenuRadioItem(hdrPresetMenu_, CommandHdrStandard, CommandHdrGentle,
            hdrPreset_ == 1 ? CommandHdrVivid :
            hdrPreset_ == 2 ? CommandHdrGentle : CommandHdrStandard,
            MF_BYCOMMAND);
    }
    if (languageMenu_) {
        CheckMenuRadioItem(languageMenu_, CommandLanguageChinese, CommandLanguageEnglish,
            IsEnglish() ? CommandLanguageEnglish : CommandLanguageChinese,
            MF_BYCOMMAND);
    }
    if (window_ && IsWindow(window_)) DrawMenuBar(window_);
}

void App::SetLanguage(const UiLanguage language) {
    if (language_ == language) return;
    language_ = language;

    HMENU oldMenu = mainMenu_;
    if (!fullscreen_) SetMenu(window_, nullptr);
    mainMenu_ = nullptr;
    viewMenu_ = nullptr;
    wheelMenu_ = nullptr;
    rtxMenu_ = nullptr;
    hdrPresetMenu_ = nullptr;
    languageMenu_ = nullptr;
    if (oldMenu) DestroyMenu(oldMenu);

    mainMenu_ = CreateApplicationMenu();
    if (!fullscreen_ && mainMenu_) SetMenu(window_, mainMenu_);
    UpdateMenuChecks();
    UpdateWindowTitle();
    InvalidateRect(window_, nullptr, FALSE);
}

void App::ShowRtxError(const std::wstring& details) {
    std::wstring message = IsEnglish()
        ? L"RTX Video Enhancement is unavailable on this computer.\n\n"
          L"Eidolyx will continue in normal image-viewing mode and will not close.\n"
          L"Verify the NVIDIA RTX GPU, compatible driver, and the required runtime DLL files."
        : L"這台電腦目前無法使用 RTX 視訊增強。\n\n"
          L"Eidolyx 會繼續以一般圖片檢視模式運作，不會因此關閉。\n"
          L"請確認 NVIDIA RTX 顯示卡、相容驅動程式與必要的 runtime DLL。";
    if (!details.empty()) {
        message += L"\n\n";
        message += IsEnglish() ? L"Details: " : L"詳細資訊：";
        message += details;
    }
    MessageBoxW(window_, message.c_str(),
        UiText(IsEnglish(), L"RTX 視訊增強無法使用", L"RTX Video Enhancement unavailable"),
        MB_OK | MB_ICONWARNING);
}

bool App::CreateDeviceIndependentResources() {
    if (FAILED(D2D1CreateFactory(
        D2D1_FACTORY_TYPE_SINGLE_THREADED, d2dFactory_.GetAddressOf()))) return false;
    if (FAILED(DWriteCreateFactory(
        DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(writeFactory_.GetAddressOf())))) return false;

    if (FAILED(writeFactory_->CreateTextFormat(
        L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, 15.0F, L"zh-TW", &textFormat_))) return false;
    if (FAILED(writeFactory_->CreateTextFormat(
        L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, 17.0F, L"zh-TW", &titleFormat_))) return false;
    textFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    titleFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    return true;
}

bool App::EnsureRenderTarget() {
    if (renderTarget_) return true;
    RECT rectangle{};
    GetClientRect(window_, &rectangle);
    const auto pixelSize = D2D1::SizeU(
        static_cast<UINT32>(std::max(1L, rectangle.right)),
        static_cast<UINT32>(std::max(1L, rectangle.bottom)));
    if (FAILED(d2dFactory_->CreateHwndRenderTarget(
        D2D1::RenderTargetProperties(),
        D2D1::HwndRenderTargetProperties(window_, pixelSize), &renderTarget_))) return false;

    renderTarget_->CreateSolidColorBrush(D2D1::ColorF(0xF4F6F8), &textBrush_);
    renderTarget_->CreateSolidColorBrush(D2D1::ColorF(0xAAB2BD), &mutedBrush_);
    renderTarget_->CreateSolidColorBrush(D2D1::ColorF(0x57A9FF), &accentBrush_);
    renderTarget_->CreateSolidColorBrush(D2D1::ColorF(0x11151A, 0.88F), &overlayBrush_);
    UploadCurrentBitmap();
    return true;
}

void App::DiscardRenderTarget() {
    bitmap_.Reset();
    textBrush_.Reset();
    mutedBrush_.Reset();
    accentBrush_.Reset();
    overlayBrush_.Reset();
    renderTarget_.Reset();
}

void App::Paint() {
    PAINTSTRUCT paint{};
    BeginPaint(window_, &paint);
    if (hdrEnabled_) {
        std::wstring error;
        bool rendered = false;
        if (EnsureRenderTarget()) {
            renderTarget_->BeginDraw();
            renderTarget_->SetTransform(D2D1::Matrix3x2F::Identity());
            renderTarget_->Clear(D2D1::ColorF(0x0B0E12));
            DrawOverlay(renderTarget_->GetSize());
            const HRESULT overlayResult = renderTarget_->EndDraw();
            if (overlayResult == D2DERR_RECREATE_TARGET) DiscardRenderTarget();
        }
        try {
            rendered = PaintHdr(error);
        } catch (const std::exception& exception) {
            error = L"HDR 呈現發生例外，已安全返回一般顯示。 / "
                    L"HDR presentation raised an exception; Eidolyx safely returned to normal display. ";
            const std::string details = exception.what();
            error.append(details.begin(), details.end());
        } catch (...) {
            error = L"HDR 呈現發生未知錯誤，已安全返回一般顯示。 / "
                    L"HDR presentation failed unexpectedly; Eidolyx safely returned to normal display.";
        }
        EndPaint(window_, &paint);
        if (!rendered) {
            DisableHdrMode();
            ShowRtxError(error);
        }
        return;
    }
    if (!EnsureRenderTarget()) {
        EndPaint(window_, &paint);
        return;
    }

    renderTarget_->BeginDraw();
    renderTarget_->SetTransform(D2D1::Matrix3x2F::Identity());
    renderTarget_->Clear(D2D1::ColorF(0x0B0E12));
    const D2D1_SIZE_F size = renderTarget_->GetSize();

    if (bitmap_ && currentImage_) {
        const auto destination = ImageRectangle(size);
        renderTarget_->DrawBitmap(
            bitmap_.Get(), destination, 1.0F, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,
            D2D1::RectF(0.0F, 0.0F, bitmap_->GetSize().width, bitmap_->GetSize().height));
    } else {
        const bool english = IsEnglish();
        const std::wstring primary = loading_
            ? UiText(english, L"正在載入圖片…", L"Loading image…")
            : UiText(english, L"拖曳圖片到這裡，或按 O 開啟", L"Drop an image here, or press O to open");
        const std::wstring secondary = folder_.Empty()
            ? UiText(english, L"開啟後會自動索引同一資料夾，並在背景預讀前後頁",
                L"The folder is indexed automatically and nearby pages are prefetched")
            : UiText(english, L"圖片解碼中；預讀完成後翻頁會直接顯示",
                L"Decoding image; prefetched pages appear immediately");
        const std::wstring rtxHint = UiText(english,
            L"RTX 視訊增強：建議將視窗最大化，或按 F11 無邊框全螢幕",
            L"RTX Video Enhancement: Maximize the window or press F11 for borderless fullscreen");
        auto primaryRect = D2D1::RectF(40.0F, size.height * 0.5F - 54.0F, size.width - 40.0F, size.height * 0.5F - 18.0F);
        auto secondaryRect = D2D1::RectF(40.0F, size.height * 0.5F - 10.0F, size.width - 40.0F, size.height * 0.5F + 24.0F);
        auto hintRect = D2D1::RectF(40.0F, size.height * 0.5F + 30.0F, size.width - 40.0F, size.height * 0.5F + 64.0F);
        titleFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        textFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        renderTarget_->DrawTextW(primary.c_str(), static_cast<UINT32>(primary.size()), titleFormat_.Get(), primaryRect, textBrush_.Get());
        renderTarget_->DrawTextW(secondary.c_str(), static_cast<UINT32>(secondary.size()), textFormat_.Get(), secondaryRect, mutedBrush_.Get());
        renderTarget_->DrawTextW(rtxHint.c_str(), static_cast<UINT32>(rtxHint.size()), textFormat_.Get(), hintRect, accentBrush_.Get());
        titleFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        textFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    }

    DrawOverlay(size);
    const HRESULT result = renderTarget_->EndDraw();
    if (result == D2DERR_RECREATE_TARGET) DiscardRenderTarget();
    EndPaint(window_, &paint);
}

void App::DrawOverlay(const D2D1_SIZE_F& size) {
    if (showInfo_ && !folder_.Empty()) {
        renderTarget_->FillRectangle(D2D1::RectF(0.0F, 0.0F, size.width, 48.0F), overlayBrush_.Get());
        renderTarget_->FillRectangle(D2D1::RectF(0.0F, size.height - 38.0F, size.width, size.height), overlayBrush_.Get());

        const std::wstring filename = folder_.Current().filename().wstring();
        renderTarget_->DrawTextW(filename.c_str(), static_cast<UINT32>(filename.size()), titleFormat_.Get(),
            D2D1::RectF(18.0F, 12.0F, size.width - 180.0F, 40.0F), textBrush_.Get());

        std::wostringstream counter;
        counter << (folder_.Index() + 1) << L" / " << folder_.Size();
        const auto counterText = counter.str();
        textFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
        renderTarget_->DrawTextW(counterText.c_str(), static_cast<UINT32>(counterText.size()), textFormat_.Get(),
            D2D1::RectF(size.width - 170.0F, 15.0F, size.width - 18.0F, 40.0F), accentBrush_.Get());
        textFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);

        std::wostringstream details;
        const bool english = IsEnglish();
        D2D1_SIZE_F scaleSize = size;
        if (hdrEnabled_ && hdrSurfaceWindow_) {
            RECT hdrClient{};
            GetClientRect(hdrSurfaceWindow_, &hdrClient);
            scaleSize = D2D1::SizeF(
                static_cast<float>(std::max(1L, hdrClient.right)),
                static_cast<float>(std::max(1L, hdrClient.bottom)));
        }
        details << ViewModeName(viewMode_, english) << L"  ·  "
                << static_cast<int>(std::lround(CurrentScale(scaleSize) * 100.0F)) << L"%";
        if (currentImage_) details << L"  ·  " << currentImage_->width << L" × " << currentImage_->height;
        if (hdrEnabled_) {
            static constexpr const wchar_t* chinesePresets[]{L"標準", L"鮮明", L"柔和"};
            static constexpr const wchar_t* englishPresets[]{L"Standard", L"Vivid", L"Gentle"};
            details << (hdrLastUsedVsr_ ? L"  ·  RTX VSR → HDR10" : L"  ·  RTX TrueHDR 10-bit");
            details << L"  ·  " << (english ? englishPresets[hdrPreset_] : chinesePresets[hdrPreset_]);
        } else if (enhancementEnabled_) {
            if (enhancementInProgress_) details << UiText(english, L"  ·  RTX 處理中", L"  ·  RTX processing");
            else if (showOriginalForComparison_ && enhancedImage_) details << UiText(english, L"  ·  RTX 比較：原圖", L"  ·  RTX comparison: Original");
            else if (enhancedImage_) details << L"  ·  RTX " << enhancedImage_->width << L" × " << enhancedImage_->height;
            else details << UiText(english, L"  ·  RTX 開啟", L"  ·  RTX enabled");
        }
        details << UiText(english,
            L"  ·  ←/→ 翻頁  Ctrl+滾輪縮放  M 最大化  F11 全螢幕  I 隱藏資訊",
            L"  ·  ←/→ pages  Ctrl+wheel zoom  M maximize  F11 fullscreen  I hide info");
        const auto detailsText = details.str();
        renderTarget_->DrawTextW(detailsText.c_str(), static_cast<UINT32>(detailsText.size()), textFormat_.Get(),
            D2D1::RectF(18.0F, size.height - 29.0F, size.width - 18.0F, size.height - 5.0F), mutedBrush_.Get());
    }

    if (!notice_.empty() && GetTickCount64() < noticeUntil_) {
        const float width = std::min(size.width - 40.0F, 680.0F);
        const float left = (size.width - width) * 0.5F;
        const auto box = D2D1::RectF(left, size.height * 0.72F, left + width, size.height * 0.72F + 52.0F);
        renderTarget_->FillRoundedRectangle(D2D1::RoundedRect(box, 8.0F, 8.0F), overlayBrush_.Get());
        textFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        renderTarget_->DrawTextW(notice_.c_str(), static_cast<UINT32>(notice_.size()), textFormat_.Get(), box, textBrush_.Get());
        textFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    }
}

void App::ShowOpenDialog() {
    ComPtr<IFileOpenDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) return;
    const COMDLG_FILTERSPEC filters[] = {
        {UiText(IsEnglish(), L"圖片", L"Images"), L"*.jpg;*.jpeg;*.jpe;*.png;*.bmp;*.gif;*.tif;*.tiff;*.webp;*.heic;*.heif;*.avif;*.jxl;*.ico;*.jxr"},
        {UiText(IsEnglish(), L"所有檔案", L"All files"), L"*.*"}
    };
    dialog->SetFileTypes(static_cast<UINT>(std::size(filters)), filters);
    dialog->SetTitle(UiText(IsEnglish(), L"開啟圖片", L"Open image"));
    if (FAILED(dialog->Show(window_))) return;

    ComPtr<IShellItem> item;
    if (FAILED(dialog->GetResult(&item))) return;
    PWSTR path = nullptr;
    if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) OpenImage(path);
    CoTaskMemFree(path);
}

bool App::OpenImage(const std::filesystem::path& file) {
    if (!folder_.Open(file)) {
        SetNotice(UiText(IsEnglish(),
            L"找不到圖片，或檔案格式不在支援清單中。",
            L"The image was not found or its format is unsupported."), 5000);
        return false;
    }
    currentImage_.reset();
    enhancedImage_.reset();
    showOriginalForComparison_ = false;
    ++enhancementGeneration_;
    bitmap_.Reset();
    zoomFactor_ = 1.0F;
    pan_ = {0.0F, 0.0F};
    loading_ = true;
    ScheduleNeighborhood();
    ApplyCurrentIfReady();
    UpdateWindowTitle();
    InvalidateRect(window_, nullptr, FALSE);
    return true;
}

void App::Navigate(const int delta) {
    if (!folder_.Move(delta)) return;
    currentImage_.reset();
    enhancedImage_.reset();
    showOriginalForComparison_ = false;
    ++enhancementGeneration_;
    bitmap_.Reset();
    zoomFactor_ = 1.0F;
    pan_ = {0.0F, 0.0F};
    loading_ = true;
    ScheduleNeighborhood();
    ApplyCurrentIfReady();
    UpdateWindowTitle();
    InvalidateRect(window_, nullptr, FALSE);
}

void App::NavigateTo(const std::size_t index) {
    if (!folder_.MoveTo(index)) return;
    currentImage_.reset();
    enhancedImage_.reset();
    showOriginalForComparison_ = false;
    ++enhancementGeneration_;
    bitmap_.Reset();
    zoomFactor_ = 1.0F;
    pan_ = {0.0F, 0.0F};
    loading_ = true;
    ScheduleNeighborhood();
    ApplyCurrentIfReady();
    UpdateWindowTitle();
    InvalidateRect(window_, nullptr, FALSE);
}

void App::ScheduleNeighborhood() {
    if (folder_.Empty()) return;
    cache_.ClearQueued();
    std::vector<std::wstring> pinned;
    pinned.reserve(17);
    const auto current = static_cast<long long>(folder_.Index());
    const auto count = static_cast<long long>(folder_.Size());
    cache_.Request(folder_.Current().wstring(), 1000);
    pinned.push_back(folder_.Current().wstring());
    for (int distance = 1; distance <= 8; ++distance) {
        const int priority = 900 - distance * 50;
        const long long next = current + distance;
        const long long previous = current - distance;
        if (next < count) {
            cache_.Request(folder_.At(static_cast<std::size_t>(next)).wstring(), priority);
            pinned.push_back(folder_.At(static_cast<std::size_t>(next)).wstring());
        }
        if (previous >= 0) {
            cache_.Request(folder_.At(static_cast<std::size_t>(previous)).wstring(), priority - 1);
            pinned.push_back(folder_.At(static_cast<std::size_t>(previous)).wstring());
        }
    }
    cache_.Pin(pinned);
}

void App::ApplyCurrentIfReady() {
    if (folder_.Empty()) return;
    const auto path = folder_.Current().wstring();
    auto image = cache_.TryGet(path);
    if (!image) {
        const auto error = cache_.ErrorFor(path);
        if (!error.empty()) {
            loading_ = false;
            SetNotice(std::wstring(UiText(IsEnglish(), L"解碼失敗：", L"Decode failed: ")) + error, 6000);
        }
        return;
    }
    if (currentImage_ && currentImage_->path == image->path) return;
    currentImage_ = std::move(image);
    loading_ = false;
    ClampHdrZoom();
    UploadCurrentBitmap();
    if (enhancementEnabled_) RequestEnhancement();
    InvalidateRect(window_, nullptr, FALSE);
}

void App::RequestEnhancement() {
    if (!enhancementEnabled_ || !enhancementWorker_ || !currentImage_) return;
#if EIDOLYX_WITH_RTX
    if (hdrRetirement_ && !hdrRetirement_->load(std::memory_order_acquire)) {
        enhancementInProgress_ = true;
        SetTimer(window_, HdrRetirementTimer, 100, nullptr);
        UpdateWindowTitle();
        InvalidateRect(window_, nullptr, FALSE);
        return;
    }
    hdrRetirement_.reset();
    KillTimer(window_, HdrRetirementTimer);
#endif
    RECT client{};
    GetClientRect(window_, &client);
    const D2D1_SIZE_F size = D2D1::SizeF(
        static_cast<float>(std::max(1L, client.right)),
        static_cast<float>(std::max(1L, client.bottom)));
    float scale = CurrentScale(size);
    if (scale <= 1.05F) {
        enhancementInProgress_ = false;
        enhancedImage_.reset();
        UploadCurrentBitmap();
        UpdateWindowTitle();
        return;
    }

    constexpr float maximumDimension = 7680.0F;
    float targetWidth = static_cast<float>(currentImage_->width) * scale;
    float targetHeight = static_cast<float>(currentImage_->height) * scale;
    if (std::max(targetWidth, targetHeight) > maximumDimension) {
        const float limitScale = maximumDimension / std::max(targetWidth, targetHeight);
        targetWidth *= limitScale;
        targetHeight *= limitScale;
    }
    const auto width = static_cast<std::uint32_t>(std::max(
        static_cast<float>(currentImage_->width + 1U), std::round(targetWidth)));
    const auto height = static_cast<std::uint32_t>(std::max(
        static_cast<float>(currentImage_->height + 1U), std::round(targetHeight)));
    enhancementInProgress_ = true;
    enhancedImage_.reset();
    UploadCurrentBitmap();
    UpdateWindowTitle();
    const std::uint64_t generation = ++enhancementGeneration_;
    enhancementWorker_->Request(currentImage_, width, height, generation);
    InvalidateRect(window_, nullptr, FALSE);
}

void App::ApplyEnhancementResult() {
    if (!enhancementWorker_) return;
    auto result = enhancementWorker_->TakeResult();
    if (result.generation == 0 || result.generation != enhancementGeneration_ ||
        !enhancementEnabled_ || folder_.Empty()) return;
    enhancementInProgress_ = false;
    if (!result.image) {
        if (!enhancer_->IsAvailable()) {
            enhancementEnabled_ = false;
            UpdateMenuChecks();
        }
        const std::wstring details = result.error.empty()
            ? UiText(IsEnglish(), L"RTX VSR 處理失敗。", L"RTX VSR processing failed.")
            : result.error;
        ShowRtxError(details);
        InvalidateRect(window_, nullptr, FALSE);
        return;
    }
    if (!currentImage_ || result.image->path != currentImage_->path) return;
    enhancedImage_ = std::move(result.image);
    UploadCurrentBitmap();
    UpdateWindowTitle();
    InvalidateRect(window_, nullptr, FALSE);
}

void App::ToggleEnhancement() {
    if (hdrEnabled_) DisableHdrMode();
    if (enhancementEnabled_) {
        enhancementEnabled_ = false;
        enhancementInProgress_ = false;
#if EIDOLYX_WITH_RTX
        KillTimer(window_, HdrRetirementTimer);
#endif
        enhancedImage_.reset();
        showOriginalForComparison_ = false;
        ++enhancementGeneration_;
        UploadCurrentBitmap();
        UpdateWindowTitle();
        UpdateMenuChecks();
        InvalidateRect(window_, nullptr, FALSE);
        return;
    }
    if (!enhancer_->IsAvailable()) {
        ShowRtxError(enhancer_->Status());
        return;
    }
    enhancementEnabled_ = true;
    UpdateMenuChecks();
    RequestEnhancement();
    if (!fullscreen_ && !IsZoomed(window_)) {
        SetNotice(UiText(IsEnglish(),
            L"RTX 視訊增強已開啟；建議將視窗最大化，或按 F11 無邊框全螢幕。",
            L"RTX Video Enhancement enabled; maximize the window or press F11 for borderless fullscreen."), 5000);
    }
}

void App::RecreateEnhancementBackend() {
    enhancementWorker_.reset();
#if EIDOLYX_WITH_RTX
    enhancer_ = std::make_unique<RtxImageEnhancer>();
#else
    enhancer_ = std::make_unique<NullImageEnhancer>();
#endif
    if (window_ && IsWindow(window_)) {
        enhancementWorker_ = std::make_unique<EnhancementWorker>(*enhancer_, window_);
    }
}

void App::ToggleHdrMode() {
    if (hdrEnabled_) {
        DisableHdrMode();
        SetNotice(UiText(IsEnglish(),
            L"已返回一般圖片顯示。", L"Returned to normal image presentation."), 3000);
        return;
    }
    if (!currentImage_) {
        SetNotice(UiText(IsEnglish(),
            L"請先開啟一張圖片，再啟動 RTX 視訊增強(VSR + HDR)。",
            L"Open an image before starting RTX Video Enhancement (VSR + HDR)."), 5000);
        return;
    }
#if EIDOLYX_WITH_RTX
    if (hdrRetirement_ && !hdrRetirement_->load(std::memory_order_acquire)) {
        SetNotice(UiText(IsEnglish(),
            L"上一個 RTX HDR 工作正在結束，完成後再試一次。",
            L"The previous RTX HDR task is finishing; try again in a moment."), 3500);
        return;
    }
    hdrRetirement_.reset();
    wchar_t executablePath[32768]{};
    if (GetModuleFileNameW(nullptr, executablePath, static_cast<DWORD>(std::size(executablePath))) == 0) {
        ShowRtxError(UiText(IsEnglish(),
            L"找不到 Eidolyx 程式路徑。", L"The Eidolyx executable path could not be found."));
        return;
    }
    const auto applicationDirectory = std::filesystem::path(executablePath).parent_path();
    if (!std::filesystem::exists(applicationDirectory / L"nvngx_vsr.dll") ||
        !std::filesystem::exists(applicationDirectory / L"nvngx_truehdr.dll")) {
        ShowRtxError(UiText(IsEnglish(),
            L"找不到 nvngx_vsr.dll 或 nvngx_truehdr.dll；請重新安裝完整版本。",
            L"nvngx_vsr.dll or nvngx_truehdr.dll is missing; reinstall the complete package."));
        return;
    }

    enhancementEnabled_ = false;
    enhancementInProgress_ = false;
    enhancedImage_.reset();
    showOriginalForComparison_ = false;
    ++enhancementGeneration_;
    enhancementWorker_.reset();
    enhancer_.reset();

    std::wstring error;
    if (!CreateHdrSurface(error)) {
        RecreateEnhancementBackend();
        ShowRtxError(error);
        return;
    }
    auto presenter = std::make_unique<RtxHdrPresenter>();
    if (!presenter->Initialize(hdrSurfaceWindow_, error)) {
        presenter.reset();
        DestroyHdrSurface();
        RecreateEnhancementBackend();
        UploadCurrentBitmap();
        ShowRtxError(error);
        UpdateMenuChecks();
        InvalidateRect(window_, nullptr, FALSE);
        return;
    }
    hdrPresenter_ = std::move(presenter);
    hdrEnabled_ = true;
    hdrLastUsedVsr_ = false;
    ClampHdrZoom();
    UpdateWindowTitle();
    UpdateMenuChecks();
    SetNotice(UiText(IsEnglish(),
        L"RTX HDR10 已在 Eidolyx 主視窗啟用；按 H 可返回一般顯示。",
        L"RTX HDR10 is active in the Eidolyx main window; press H to return to normal presentation."), 5000);
    InvalidateRect(window_, nullptr, FALSE);
#else
    ShowRtxError(UiText(IsEnglish(),
        L"此版本未包含 NVIDIA RTX Video SDK。",
        L"This build does not include the NVIDIA RTX Video SDK."));
#endif
}

void App::DisableHdrMode(const bool restoreSdrBackend) {
    if (!hdrEnabled_
#if EIDOLYX_WITH_RTX
        && !hdrPresenter_
#endif
    ) return;
#if EIDOLYX_WITH_RTX
    if (hdrPresenter_) {
        hdrRetirement_ = hdrPresenter_->Shutdown();
        hdrPresenter_.reset();
    }
#endif
    DestroyHdrSurface();
    hdrEnabled_ = false;
    hdrLastUsedVsr_ = false;
    if (restoreSdrBackend) {
        RecreateEnhancementBackend();
        UploadCurrentBitmap();
    }
    UpdateWindowTitle();
    UpdateMenuChecks();
    RedrawWindow(window_, nullptr, nullptr,
        RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW | RDW_ALLCHILDREN);
}

void App::SetHdrPreset(const int preset) {
    hdrPreset_ = std::clamp(preset, 0, 2);
#if EIDOLYX_WITH_RTX
    if (hdrPresenter_) hdrPresenter_->Invalidate();
#endif
    UpdateMenuChecks();
    UpdateWindowTitle();
    InvalidateRect(window_, nullptr, FALSE);
}

bool App::PaintHdr(std::wstring& error) {
#if EIDOLYX_WITH_RTX
    if (!hdrEnabled_ || !hdrPresenter_) return false;
    RECT client{};
    GetClientRect(hdrSurfaceWindow_, &client);
    RECT destination{0, 0, std::max(1L, client.right), std::max(1L, client.bottom)};
    if (currentImage_) {
        const D2D1_SIZE_F size = D2D1::SizeF(
            static_cast<float>(std::max(1L, client.right)),
            static_cast<float>(std::max(1L, client.bottom)));
        const D2D1_RECT_F image = ImageRectangle(size);
        destination = {
            static_cast<LONG>(std::lround(image.left)),
            static_cast<LONG>(std::lround(image.top)),
            static_cast<LONG>(std::lround(image.right)),
            static_cast<LONG>(std::lround(image.bottom))};
    }
    bool usedVsr = false;
    if (!hdrPresenter_->Render(currentImage_, destination,
            static_cast<HdrPreset>(hdrPreset_), usedVsr, error)) return false;
    if (usedVsr != hdrLastUsedVsr_) {
        hdrLastUsedVsr_ = usedVsr;
        UpdateWindowTitle();
    }
    return true;
#else
    error = L"此版本未包含 NVIDIA RTX Video SDK。 / This build does not include NVIDIA RTX Video SDK.";
    return false;
#endif
}

void App::ClampHdrZoom() {
    if (!hdrEnabled_ || !currentImage_) return;
    RECT client{};
    GetClientRect(window_, &client);
    const D2D1_SIZE_F size = D2D1::SizeF(
        static_cast<float>(std::max(1L, client.right)),
        static_cast<float>(std::max(1L, client.bottom)));
    const float baseScale = BaseScale(size);
    const float sourceMaximum = static_cast<float>(
        std::max(currentImage_->width, currentImage_->height));
    if (baseScale <= 0.0F || sourceMaximum <= 0.0F) return;
    constexpr float maximumDimension = 7680.0F;
    const float maximumZoom = maximumDimension / (sourceMaximum * baseScale);
    zoomFactor_ = std::min(zoomFactor_, std::max(0.02F, maximumZoom));
}

void App::UploadCurrentBitmap() {
    bitmap_.Reset();
#if EIDOLYX_WITH_RTX
    if (hdrEnabled_) {
        if (hdrPresenter_) hdrPresenter_->Invalidate();
        return;
    }
#endif
    const auto image = enhancementEnabled_ && enhancedImage_ && !showOriginalForComparison_
        ? enhancedImage_ : currentImage_;
    if (!renderTarget_ || !image || image->pixels.empty()) return;
    const auto properties = D2D1::BitmapProperties(
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    const HRESULT result = renderTarget_->CreateBitmap(
        D2D1::SizeU(image->width, image->height), image->pixels.data(),
        image->stride, properties, &bitmap_);
    if (FAILED(result)) SetNotice(UiText(IsEnglish(),
        L"無法將圖片上傳到 Direct2D。", L"The image could not be uploaded to Direct2D."), 5000);
}

void App::SetViewMode(const ViewMode mode) {
    viewMode_ = mode;
    zoomFactor_ = 1.0F;
    pan_ = {0.0F, 0.0F};
    ClampHdrZoom();
    enhancedImage_.reset();
    ++enhancementGeneration_;
    UploadCurrentBitmap();
    if (enhancementEnabled_) RequestEnhancement();
    UpdateMenuChecks();
    InvalidateRect(window_, nullptr, FALSE);
}

void App::SetWheelBehavior(const WheelBehavior behavior) {
    wheelBehavior_ = behavior;
    UpdateMenuChecks();
}

void App::ZoomAt(const float factor, const POINT clientPoint) {
    if (!currentImage_ || factor <= 0.0F) return;
    const D2D1_SIZE_F size = renderTarget_ ? renderTarget_->GetSize() : D2D1::SizeF(
        ClientRectF(window_).right, ClientRectF(window_).bottom);
    const float oldScale = CurrentScale(size);
    const float oldZoom = zoomFactor_;
    zoomFactor_ = std::clamp(zoomFactor_ * factor, 0.02F, 50.0F);
    ClampHdrZoom();
    const float newScale = CurrentScale(size);
    if (oldScale <= 0.0F || std::abs(oldZoom - zoomFactor_) < 0.0001F) return;
    const D2D1_POINT_2F center{size.width * 0.5F, size.height * 0.5F};
    const D2D1_POINT_2F point{static_cast<float>(clientPoint.x), static_cast<float>(clientPoint.y)};
    const D2D1_POINT_2F imagePoint{
        (point.x - center.x - pan_.x) / oldScale,
        (point.y - center.y - pan_.y) / oldScale
    };
    pan_.x = point.x - center.x - imagePoint.x * newScale;
    pan_.y = point.y - center.y - imagePoint.y * newScale;
    InvalidateRect(window_, nullptr, FALSE);
}

float App::BaseScale(const D2D1_SIZE_F& clientSize) const {
    if (!currentImage_ || currentImage_->width == 0 || currentImage_->height == 0) return 1.0F;
    const float widthScale = clientSize.width / static_cast<float>(currentImage_->width);
    const float heightScale = clientSize.height / static_cast<float>(currentImage_->height);
    switch (viewMode_) {
    case ViewMode::FitWidth: return widthScale;
    case ViewMode::ActualSize: return 1.0F;
    default: return std::min(widthScale, heightScale);
    }
}

float App::CurrentScale(const D2D1_SIZE_F& clientSize) const {
    return BaseScale(clientSize) * zoomFactor_;
}

D2D1_RECT_F App::ImageRectangle(const D2D1_SIZE_F& clientSize) const {
    if (!currentImage_) return D2D1::RectF();
    const float scale = CurrentScale(clientSize);
    const float width = static_cast<float>(currentImage_->width) * scale;
    const float height = static_cast<float>(currentImage_->height) * scale;
    const float left = (clientSize.width - width) * 0.5F + pan_.x;
    const float top = (clientSize.height - height) * 0.5F + pan_.y;
    return D2D1::RectF(left, top, left + width, top + height);
}

void App::ToggleMaximized() {
    if (fullscreen_) ToggleFullscreen();
    ShowWindow(window_, IsZoomed(window_) ? SW_RESTORE : SW_MAXIMIZE);
    UpdateMenuChecks();
}

void App::ToggleFullscreen() {
    if (!fullscreen_) {
        windowStyle_ = static_cast<DWORD>(GetWindowLongPtrW(window_, GWL_STYLE));
        windowPlacement_.length = sizeof(WINDOWPLACEMENT);
        GetWindowPlacement(window_, &windowPlacement_);
        MONITORINFO monitor{sizeof(MONITORINFO)};
        GetMonitorInfoW(MonitorFromWindow(window_, MONITOR_DEFAULTTONEAREST), &monitor);
        SetWindowLongPtrW(window_, GWL_STYLE, windowStyle_ & ~WS_OVERLAPPEDWINDOW);
        SetMenu(window_, nullptr);
        SetWindowPos(window_, HWND_TOP, monitor.rcMonitor.left, monitor.rcMonitor.top,
            monitor.rcMonitor.right - monitor.rcMonitor.left, monitor.rcMonitor.bottom - monitor.rcMonitor.top,
            SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
        fullscreen_ = true;
        ArmFullscreenCursorHide();
    } else {
        SetWindowLongPtrW(window_, GWL_STYLE, windowStyle_);
        SetMenu(window_, mainMenu_);
        SetWindowPlacement(window_, &windowPlacement_);
        SetWindowPos(window_, nullptr, 0, 0, 0, 0,
            SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER);
        fullscreen_ = false;
        KillTimer(window_, FullscreenCursorTimer);
        ShowFullscreenCursor();
    }
    UpdateMenuChecks();
}

void App::ArmFullscreenCursorHide() {
    ShowFullscreenCursor();
    KillTimer(window_, FullscreenCursorTimer);
    if (fullscreen_) {
        SetTimer(window_, FullscreenCursorTimer, FullscreenCursorDelayMilliseconds, nullptr);
    }
}

void App::ShowFullscreenCursor() noexcept {
    if (!fullscreenCursorHidden_) return;
    for (unsigned int call = 0; call < fullscreenCursorHideCalls_; ++call) {
        ShowCursor(TRUE);
    }
    fullscreenCursorHideCalls_ = 0;
    SetCursor(LoadCursorW(nullptr, IDC_ARROW));
    fullscreenCursorHidden_ = false;
}

void App::HideFullscreenCursorIfEligible() {
    KillTimer(window_, FullscreenCursorTimer);
    if (!fullscreen_ || GetForegroundWindow() != window_) {
        ShowFullscreenCursor();
        return;
    }
    POINT cursor{};
    if (!GetCursorPos(&cursor)) return;
    const HMONITOR appMonitor = MonitorFromWindow(window_, MONITOR_DEFAULTTONEAREST);
    const HMONITOR cursorMonitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONULL);
    if (!appMonitor || cursorMonitor != appMonitor) {
        ShowFullscreenCursor();
        return;
    }
    if (!fullscreenCursorHidden_) {
        int displayCount = 0;
        do {
            displayCount = ShowCursor(FALSE);
            ++fullscreenCursorHideCalls_;
        } while (displayCount >= 0);
        fullscreenCursorHidden_ = true;
    }
    SetCursor(nullptr);
}

void App::ShowContextMenu(const POINT screenPoint) {
    const bool english = IsEnglish();
    ShowFullscreenCursor();
    KillTimer(window_, FullscreenCursorTimer);
    HMENU menu = CreatePopupMenu();
    HMENU hdrModeMenu = CreatePopupMenu();
    if (!menu) {
        if (hdrModeMenu) DestroyMenu(hdrModeMenu);
        if (fullscreen_) ArmFullscreenCursorHide();
        return;
    }
    AppendMenuW(menu, MF_STRING, CommandOpen,
        UiText(english, L"開啟圖片…\tO", L"Open image…\tO"));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, CommandPrevious,
        UiText(english, L"上一張\t←", L"Previous\t←"));
    AppendMenuW(menu, MF_STRING, CommandNext,
        UiText(english, L"下一張\t→", L"Next\t→"));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (viewMode_ == ViewMode::FitWindow ? MF_CHECKED : 0), CommandFit,
        UiText(english, L"適合視窗\t1", L"Fit window\t1"));
    AppendMenuW(menu, MF_STRING | (viewMode_ == ViewMode::FitWidth ? MF_CHECKED : 0), CommandFitWidth,
        UiText(english, L"適合寬度\t2", L"Fit width\t2"));
    AppendMenuW(menu, MF_STRING | (viewMode_ == ViewMode::ActualSize ? MF_CHECKED : 0), CommandActual,
        UiText(english, L"原始大小\t3", L"Actual size\t3"));
    AppendMenuW(menu, MF_STRING | (IsZoomed(window_) && !fullscreen_ ? MF_CHECKED : 0), CommandMaximize,
        UiText(english, L"視窗最大化\tM", L"Maximize window\tM"));
    AppendMenuW(menu, MF_STRING | (fullscreen_ ? MF_CHECKED : 0), CommandFullscreen,
        UiText(english, L"無邊框全螢幕\tF11", L"Borderless fullscreen\tF11"));
    AppendMenuW(menu, MF_STRING | (showInfo_ ? MF_CHECKED : 0), CommandToggleInfo,
        UiText(english, L"資訊列\tI", L"Information bar\tI"));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (enhancementEnabled_ ? MF_CHECKED : 0), CommandRtx,
        UiText(english, L"RTX VSR 超解析度\tR", L"RTX VSR Super Resolution\tR"));
    AppendMenuW(menu, MF_STRING | (hdrEnabled_ ? MF_CHECKED : 0), CommandHdr,
        UiText(english, L"RTX 視訊增強(VSR + HDR)\tH",
            L"RTX Video Enhancement (VSR + HDR)\tH"));
    if (hdrModeMenu) {
        AppendMenuW(hdrModeMenu, MF_STRING, CommandHdrStandard,
            UiText(english, L"標準", L"Standard"));
        AppendMenuW(hdrModeMenu, MF_STRING, CommandHdrVivid,
            UiText(english, L"鮮明", L"Vivid"));
        AppendMenuW(hdrModeMenu, MF_STRING, CommandHdrGentle,
            UiText(english, L"柔和", L"Gentle"));
        CheckMenuRadioItem(hdrModeMenu, CommandHdrStandard, CommandHdrGentle,
            hdrPreset_ == 1 ? CommandHdrVivid :
            hdrPreset_ == 2 ? CommandHdrGentle : CommandHdrStandard,
            MF_BYCOMMAND);
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(hdrModeMenu),
            UiText(english, L"HDR 模式", L"HDR mode"));
    }
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, CommandExit, UiText(english, L"結束", L"Exit"));
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, screenPoint.x, screenPoint.y, 0, window_, nullptr);
    DestroyMenu(menu);
    if (fullscreen_) ArmFullscreenCursorHide();
}

void App::SetNotice(std::wstring text, const DWORD milliseconds) {
    notice_ = std::move(text);
    noticeUntil_ = GetTickCount64() + milliseconds;
    SetTimer(window_, NoticeTimer, std::min<DWORD>(milliseconds, 1000), nullptr);
    InvalidateRect(window_, nullptr, FALSE);
}

void App::UpdateWindowTitle() {
    std::wstring title = ProductName;
    if (!folder_.Empty()) {
        title = folder_.Current().filename().wstring();
        if (hdrEnabled_) {
            static constexpr const wchar_t* chinesePresets[]{L"標準", L"鮮明", L"柔和"};
            static constexpr const wchar_t* englishPresets[]{L"Standard", L"Vivid", L"Gentle"};
            title += hdrLastUsedVsr_ ? L" [RTX VSR → HDR10 · " : L" [RTX TrueHDR 10-bit · ";
            title += IsEnglish() ? englishPresets[hdrPreset_] : chinesePresets[hdrPreset_];
            title += L"]";
        } else if (enhancementEnabled_ && enhancedImage_) {
            title += L" [RTX]";
        }
        title += L" — " + std::wstring(ProductName);
    }
    SetWindowTextW(window_, title.c_str());
}

std::filesystem::path App::SettingsPath() const {
    PWSTR localAppData = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &localAppData))) return {};
    std::filesystem::path path(localAppData);
    CoTaskMemFree(localAppData);
    return path / L"Eidolyx" / L"settings.ini";
}

void App::LoadSettings() {
    auto path = SettingsPath();
    if (path.empty()) return;
    std::error_code settingsError;
    if (!std::filesystem::exists(path, settingsError)) {
        const auto legacyPath = path.parent_path().parent_path() / L"MiraView" / L"settings.ini";
        settingsError.clear();
        if (std::filesystem::exists(legacyPath, settingsError)) path = legacyPath;
    }
    const auto native = path.wstring();
    showInfo_ = GetPrivateProfileIntW(L"view", L"showInfo", 1, native.c_str()) != 0;
    const int mode = GetPrivateProfileIntW(L"view", L"mode", 0, native.c_str());
    if (mode >= 0 && mode <= 2) viewMode_ = static_cast<ViewMode>(mode);
    const int wheel = GetPrivateProfileIntW(L"input", L"wheelBehavior", 0, native.c_str());
    if (wheel >= 0 && wheel <= 1) wheelBehavior_ = static_cast<WheelBehavior>(wheel);
    const int language = GetPrivateProfileIntW(L"ui", L"language", 0, native.c_str());
    if (language >= 0 && language <= 1) language_ = static_cast<UiLanguage>(language);
    const int hdrPreset = GetPrivateProfileIntW(L"rtx", L"hdrPreset", 0, native.c_str());
    if (hdrPreset >= 0 && hdrPreset <= 2) hdrPreset_ = hdrPreset;
    const int layoutVersion = GetPrivateProfileIntW(L"settings", L"layoutVersion", 0, native.c_str());
    const int left = GetPrivateProfileIntW(L"window", L"left", INT_MIN, native.c_str());
    const int top = GetPrivateProfileIntW(L"window", L"top", INT_MIN, native.c_str());
    const int width = GetPrivateProfileIntW(L"window", L"width", 0, native.c_str());
    const int height = GetPrivateProfileIntW(L"window", L"height", 0, native.c_str());
    if (layoutVersion >= LayoutSettingsVersion &&
        left != INT_MIN && top != INT_MIN && width >= 640 && height >= 480) {
        RECT proposed{left, top, left + width, top + height};
        if (MonitorFromRect(&proposed, MONITOR_DEFAULTTONULL)) MoveWindow(window_, left, top, width, height, FALSE);
    }
}

void App::SaveSettings() const {
    const auto path = SettingsPath();
    if (path.empty()) return;
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    const auto native = path.wstring();
    RECT rectangle{};
    if (fullscreen_) {
        rectangle = windowPlacement_.rcNormalPosition;
    } else if (IsZoomed(window_)) {
        WINDOWPLACEMENT placement{sizeof(WINDOWPLACEMENT)};
        if (GetWindowPlacement(window_, &placement)) rectangle = placement.rcNormalPosition;
    } else {
        GetWindowRect(window_, &rectangle);
    }
    const auto write = [&](const wchar_t* section, const wchar_t* key, const int value) {
        const auto text = std::to_wstring(value);
        WritePrivateProfileStringW(section, key, text.c_str(), native.c_str());
    };
    write(L"window", L"left", rectangle.left);
    write(L"window", L"top", rectangle.top);
    write(L"window", L"width", rectangle.right - rectangle.left);
    write(L"window", L"height", rectangle.bottom - rectangle.top);
    write(L"view", L"showInfo", showInfo_ ? 1 : 0);
    write(L"view", L"mode", static_cast<int>(viewMode_));
    write(L"input", L"wheelBehavior", static_cast<int>(wheelBehavior_));
    write(L"ui", L"language", static_cast<int>(language_));
    write(L"rtx", L"hdrPreset", hdrPreset_);
    write(L"settings", L"layoutVersion", LayoutSettingsVersion);
}

LRESULT CALLBACK App::WindowProcedure(const HWND window, const UINT message, const WPARAM wParam, const LPARAM lParam) {
    App* app = reinterpret_cast<App*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        app = static_cast<App*>(create->lpCreateParams);
        app->window_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    }
    if (!app) return DefWindowProcW(window, message, wParam, lParam);
    const LRESULT result = app->HandleMessage(message, wParam, lParam);
    if (message == WM_NCDESTROY) SetWindowLongPtrW(window, GWLP_USERDATA, 0);
    return result;
}

LRESULT CALLBACK App::HdrSurfaceProcedure(
    const HWND window, const UINT message, const WPARAM wParam, const LPARAM lParam) {
    switch (message) {
#if EIDOLYX_WITH_RTX
    case RtxHdrPresenter::FrameReadyMessage:
        if (const HWND parent = GetParent(window); parent && IsWindow(parent)) {
            PostMessageW(parent, message, wParam, lParam);
        }
        return 0;
#endif
    case WM_NCHITTEST:
        return HTTRANSPARENT;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_ERASEBKGND:
        return 1;
    default:
        return DefWindowProcW(window, message, wParam, lParam);
    }
}

LRESULT App::HandleMessage(const UINT message, const WPARAM wParam, const LPARAM lParam) {
    switch (message) {
    case WM_PAINT:
        Paint();
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_SIZE:
        if (renderTarget_) renderTarget_->Resize(D2D1::SizeU(LOWORD(lParam), HIWORD(lParam)));
        if (hdrEnabled_) UpdateHdrSurfaceBounds();
        if (hdrEnabled_ && wParam != SIZE_MINIMIZED && currentImage_) ClampHdrZoom();
        if (enhancementEnabled_ && wParam != SIZE_MINIMIZED && currentImage_) {
            SetTimer(window_, EnhancementResizeTimer, 250, nullptr);
        }
        UpdateMenuChecks();
        InvalidateRect(window_, nullptr, FALSE);
        return 0;
    case WM_DPICHANGED: {
        const auto* rectangle = reinterpret_cast<RECT*>(lParam);
        SetWindowPos(window_, nullptr, rectangle->left, rectangle->top,
            rectangle->right - rectangle->left, rectangle->bottom - rectangle->top,
            SWP_NOACTIVATE | SWP_NOZORDER);
        if (hdrEnabled_) UpdateHdrSurfaceBounds();
        return 0;
    }
    case ImageCache::ImageReadyMessage:
        ApplyCurrentIfReady();
        return 0;
    case EnhancementWorker::EnhancementReadyMessage:
        ApplyEnhancementResult();
        return 0;
#if EIDOLYX_WITH_RTX
    case RtxHdrPresenter::FrameReadyMessage:
        if (hdrEnabled_) InvalidateRect(window_, nullptr, FALSE);
        return 0;
#endif
    case WM_DROPFILES: {
        const HDROP drop = reinterpret_cast<HDROP>(wParam);
        wchar_t path[32768]{};
        if (DragQueryFileW(drop, 0, path, static_cast<UINT>(std::size(path))) > 0) OpenImage(path);
        DragFinish(drop);
        return 0;
    }
    case WM_LBUTTONDBLCLK:
        ToggleFullscreen();
        return 0;
    case WM_LBUTTONDOWN:
        dragging_ = true;
        dragOrigin_ = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        panOrigin_ = pan_;
        SetCapture(window_);
        return 0;
    case WM_MOUSEMOVE:
        if (fullscreen_) ArmFullscreenCursorHide();
        else ShowFullscreenCursor();
        if (dragging_) {
            pan_.x = panOrigin_.x + static_cast<float>(GET_X_LPARAM(lParam) - dragOrigin_.x);
            pan_.y = panOrigin_.y + static_cast<float>(GET_Y_LPARAM(lParam) - dragOrigin_.y);
            InvalidateRect(window_, nullptr, FALSE);
        }
        return 0;
    case WM_LBUTTONUP:
        if (dragging_) {
            dragging_ = false;
            ReleaseCapture();
        }
        return 0;
    case WM_CAPTURECHANGED:
        dragging_ = false;
        return 0;
    case WM_MOUSEWHEEL: {
        const int steps = GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA;
        const bool controlZoom = (GET_KEYSTATE_WPARAM(wParam) & MK_CONTROL) != 0;
        if (wheelBehavior_ == WheelBehavior::Zoom || controlZoom) {
            POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            ScreenToClient(window_, &point);
            ZoomAt(std::pow(1.15F, static_cast<float>(steps)), point);
        } else if (steps != 0) {
            Navigate(-steps);
        }
        return 0;
    }
    case WM_CONTEXTMENU: {
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        if (point.x == -1 && point.y == -1) {
            RECT rectangle{};
            GetWindowRect(window_, &rectangle);
            point = {(rectangle.left + rectangle.right) / 2, (rectangle.top + rectangle.bottom) / 2};
        }
        ShowContextMenu(point);
        return 0;
    }
    case WM_SETCURSOR:
        if (fullscreen_ && fullscreenCursorHidden_ && LOWORD(lParam) == HTCLIENT) {
            SetCursor(nullptr);
            return TRUE;
        }
        break;
    case WM_KEYDOWN: {
        switch (wParam) {
        case VK_LEFT: case VK_PRIOR: case VK_BACK: Navigate(-1); return 0;
        case VK_RIGHT: case VK_NEXT: case VK_SPACE: Navigate(1); return 0;
        case VK_HOME: if (!folder_.Empty()) NavigateTo(0); return 0;
        case VK_END: if (!folder_.Empty()) NavigateTo(folder_.Size() - 1); return 0;
        case VK_F11: ToggleFullscreen(); return 0;
        case VK_ESCAPE: if (fullscreen_) ToggleFullscreen(); return 0;
        case VK_ADD: case VK_OEM_PLUS: {
            RECT rectangle{}; GetClientRect(window_, &rectangle);
            ZoomAt(1.15F, POINT{rectangle.right / 2, rectangle.bottom / 2}); return 0;
        }
        case VK_SUBTRACT: case VK_OEM_MINUS: {
            RECT rectangle{}; GetClientRect(window_, &rectangle);
            ZoomAt(1.0F / 1.15F, POINT{rectangle.right / 2, rectangle.bottom / 2}); return 0;
        }
        case L'O': ShowOpenDialog(); return 0;
        case L'I':
            showInfo_ = !showInfo_;
            if (hdrEnabled_) UpdateHdrSurfaceBounds();
            UpdateMenuChecks();
            InvalidateRect(window_, nullptr, FALSE);
            return 0;
        case L'R': ToggleEnhancement(); return 0;
        case L'H': ToggleHdrMode(); return 0;
        case L'M': ToggleMaximized(); return 0;
        case L'C':
            if (enhancementEnabled_ && enhancedImage_ && !showOriginalForComparison_) {
                showOriginalForComparison_ = true;
                UploadCurrentBitmap();
                InvalidateRect(window_, nullptr, FALSE);
            }
            return 0;
        case L'1': SetViewMode(ViewMode::FitWindow); return 0;
        case L'2': SetViewMode(ViewMode::FitWidth); return 0;
        case L'3': SetViewMode(ViewMode::ActualSize); return 0;
        case L'0': SetViewMode(ViewMode::FitWindow); return 0;
        default: break;
        }
        break;
    }
    case WM_KEYUP:
        if (wParam == L'C' && showOriginalForComparison_) {
            showOriginalForComparison_ = false;
            UploadCurrentBitmap();
            InvalidateRect(window_, nullptr, FALSE);
            return 0;
        }
        break;
    case WM_KILLFOCUS:
        KillTimer(window_, FullscreenCursorTimer);
        ShowFullscreenCursor();
        if (showOriginalForComparison_) {
            showOriginalForComparison_ = false;
            UploadCurrentBitmap();
            InvalidateRect(window_, nullptr, FALSE);
        }
        return 0;
    case WM_SETFOCUS:
        if (fullscreen_) ArmFullscreenCursorHide();
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case CommandOpen: ShowOpenDialog(); break;
        case CommandPrevious: Navigate(-1); break;
        case CommandNext: Navigate(1); break;
        case CommandFirst: if (!folder_.Empty()) NavigateTo(0); break;
        case CommandLast: if (!folder_.Empty()) NavigateTo(folder_.Size() - 1); break;
        case CommandFit: SetViewMode(ViewMode::FitWindow); break;
        case CommandFitWidth: SetViewMode(ViewMode::FitWidth); break;
        case CommandActual: SetViewMode(ViewMode::ActualSize); break;
        case CommandMaximize: ToggleMaximized(); break;
        case CommandFullscreen: ToggleFullscreen(); break;
        case CommandToggleInfo:
            showInfo_ = !showInfo_;
            if (hdrEnabled_) UpdateHdrSurfaceBounds();
            UpdateMenuChecks();
            InvalidateRect(window_, nullptr, FALSE);
            break;
        case CommandRtx: ToggleEnhancement(); break;
        case CommandHdr: ToggleHdrMode(); break;
        case CommandHdrStandard: SetHdrPreset(0); break;
        case CommandHdrVivid: SetHdrPreset(1); break;
        case CommandHdrGentle: SetHdrPreset(2); break;
        case CommandWheelNavigate: SetWheelBehavior(WheelBehavior::Navigate); break;
        case CommandWheelZoom: SetWheelBehavior(WheelBehavior::Zoom); break;
        case CommandLanguageChinese: SetLanguage(UiLanguage::TraditionalChinese); break;
        case CommandLanguageEnglish: SetLanguage(UiLanguage::English); break;
        case CommandAbout:
            MessageBoxW(window_,
                L"Eidolyx 0.5.0 (RTX VSR + HDR)\n\n"
                L"【繁體中文】\n"
                L"針對漫畫文字與圖片放大的原生 Windows 圖片檢視器，支援 NVIDIA RTX Video VSR 與 HDR。\n"
                L"Eidolyx 是由希臘文 eidolon（影像）與拉丁文 lux（光）延伸的新造字，不是縮寫。\n"
                L"VSR 會依目前顯示尺寸輸出，最高單邊 7680 像素。\n"
                L"使用 NVIDIA RTX Video SDK 1.1。\n"
                L"按 H 可在原本主視窗切換 VSR → TrueHDR 10-bit 顯示。\n"
                L"按 M 將視窗最大化；按 F11 使用無邊框全螢幕。\n"
                L"F11 全螢幕閒置 3 秒會隱藏同螢幕游標；右鍵可選擇 HDR 模式。\n"
                L"RTX 套用後可按住 C 比較原圖。\n"
                L"本專案不代表受到 NVIDIA 贊助或背書。\n\n"
                L"[English]\n"
                L"Native Windows image viewer for enlarged comic text and images, with NVIDIA RTX Video VSR and HDR.\n"
                L"Eidolyx is a coined name derived from the Greek eidolon (image) and Latin lux (light); it is not an acronym.\n"
                L"VSR output follows the current display size, up to 7680 pixels per side.\n"
                L"Built with NVIDIA RTX Video SDK 1.1.\n"
                L"Press H to toggle VSR to TrueHDR 10-bit output in the original main window.\n"
                L"Press M to maximize the window or F11 for borderless fullscreen.\n"
                L"F11 fullscreen hides an idle cursor after 3 seconds; right-click selects the HDR mode.\n"
                L"Hold C to compare the original image.\n"
                L"This project is not sponsored or endorsed by NVIDIA.",
                L"關於 Eidolyx / About Eidolyx", MB_OK | MB_ICONINFORMATION);
            break;
        case CommandExit: SendMessageW(window_, WM_CLOSE, 0, 0); break;
        default: break;
        }
        return 0;
    case WM_TIMER:
        if (wParam == FullscreenCursorTimer) {
            HideFullscreenCursorIfEligible();
            return 0;
        }
#if EIDOLYX_WITH_RTX
        if (wParam == HdrRetirementTimer) {
            if (!hdrRetirement_ || hdrRetirement_->load(std::memory_order_acquire)) {
                KillTimer(window_, HdrRetirementTimer);
                hdrRetirement_.reset();
                enhancementInProgress_ = false;
                if (enhancementEnabled_ && currentImage_) RequestEnhancement();
            }
            return 0;
        }
#endif
        if (wParam == EnhancementResizeTimer) {
            KillTimer(window_, EnhancementResizeTimer);
            if (enhancementEnabled_ && currentImage_) RequestEnhancement();
            return 0;
        }
        if (wParam == NoticeTimer && GetTickCount64() >= noticeUntil_) {
            KillTimer(window_, NoticeTimer);
            notice_.clear();
            InvalidateRect(window_, nullptr, FALSE);
        }
        return 0;
    case WM_CLOSE:
        SaveSettings();
        DestroyWindow(window_);
        return 0;
    case WM_DESTROY:
        KillTimer(window_, FullscreenCursorTimer);
        ShowFullscreenCursor();
        cache_.SetNotificationWindow(nullptr);
#if EIDOLYX_WITH_RTX
        if (hdrPresenter_) {
            (void)hdrPresenter_->Shutdown();
            hdrPresenter_.reset();
        }
#endif
        DestroyHdrSurface();
        hdrEnabled_ = false;
        enhancementWorker_.reset();
        if (mainMenu_) {
            SetMenu(window_, nullptr);
            DestroyMenu(mainMenu_);
            mainMenu_ = nullptr;
            viewMenu_ = nullptr;
            wheelMenu_ = nullptr;
            rtxMenu_ = nullptr;
            hdrPresetMenu_ = nullptr;
            languageMenu_ = nullptr;
        }
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(window_, message, wParam, lParam);
}
