#include "App.h"
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

// In-process integration test. No global input injection or user-window automation.
struct ReaderUiTest {
    static void Require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
    template<class Predicate> static void Await(App& app, Predicate ready, const char* message) {
        const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        while (!ready() && std::chrono::steady_clock::now() < until) {
            MSG msg{};
            while (PeekMessageW(&msg, app.window_, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        Require(ready(), message);
    }
    static void Run(const std::filesystem::path& fixtures) {
        App app;
        Require(app.CreateDeviceIndependentResources() && app.CreateMainWindow(GetModuleHandleW(nullptr)), "create hidden reader window");
        struct WindowScope { App& app; ~WindowScope() { if (IsWindow(app.window_)) DestroyWindow(app.window_); } } scope{app};
        app.mainMenu_ = app.CreateApplicationMenu(); SetMenu(app.window_, app.mainMenu_);
        Require(app.OpenImage(fixtures / L"001.png"), "open PNG");
        Await(app, [&] { return app.currentImage_ != nullptr; }, "first PNG decode");
        app.SetContinuous(true);
        Require(app.continuous_ && !app.hdrEnabled_, "enable continuous mode");
        Await(app, [&] { return app.cache_.TryGet((fixtures / L"003.png").wstring()) != nullptr; }, "long page prefetch");
        app.RefreshContinuous();
        Require(app.EnsureRenderTarget(), "Direct2D target");
        app.JumpContinuous(0);
        app.ScrollContinuous(app.layout_.Top(1) * app.ReaderWidth() - app.ReaderHeight() * 0.4);
        app.Paint();
        Require(app.readerTiles_.size() >= 2, "multiple pages visible across seam");
        const auto before = app.scrollPosition_;
        app.HandleMessage(WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(-30)), 0);
        Require(app.scrollPosition_ > before, "high-resolution wheel moves below 120 delta");
        app.HandleMessage(WM_KEYDOWN, VK_HOME, 0);
        Require(app.scrollPosition_ == 0, "Home reaches top");
        app.HandleMessage(WM_KEYDOWN, VK_END, 0);
        Require(app.scrollPosition_ > 20, "End reaches end of tall page");
        app.Paint(); Require(!app.readerTiles_.empty(), "tall image tiled render");
        app.JumpContinuous(0);
        app.ZoomAt(1.2F, POINT{400, 300});
        Require(app.readerZoom_ > 1.1F, "continuous zoom");
        app.SetContinuous(false);
        Require(!app.continuous_ && app.currentImage_, "return to single-page image");
        app.SetLanguage(UiLanguage::English);
        app.StartBookOpen(fixtures / L"閱讀測試.epub");
        Await(app, [&] { return !app.bookFuture_.valid(); }, "async EPUB import");
        Require(app.folder_.Book() && app.folder_.Size() == 3 && app.continuous_, "EPUB automatically enters continuous mode");
        Await(app, [&] { return app.currentImage_ && app.currentImage_->height == 600; }, "spine first page");
        app.Navigate(1);
        Await(app, [&] { return app.currentImage_ && app.currentImage_->height == 900; }, "EPUB next page");
        auto extracted = app.folder_.Current().parent_path();
        app.OpenImage(fixtures / L"001.png");
        Require(!app.folder_.Book(), "replace EPUB with folder");
        Await(app, [&] { return !std::filesystem::exists(extracted); }, "old EPUB lease cleanup");
        app.StartBookOpen(fixtures / L"閱讀測試.epub");
        app.HandleMessage(WM_KEYDOWN, VK_ESCAPE, 0);
        Await(app, [&] { return !app.bookFuture_.valid(); }, "EPUB cancel");
        Require(!app.folder_.Book(), "cancel retains current folder");
        std::cout << "PASS: hidden Win32/Direct2D reader, page seam, tall-image tiles, precision wheel, Home/End, zoom, single-page transition, English menu, async EPUB navigation, source replacement, cancellation, temp cleanup.\n";
    }
};
int wmain(int argc, wchar_t** argv) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    int result = 0;
    try { ReaderUiTest::Run(std::filesystem::absolute(argc > 1 ? argv[1] : L"build/reader-probe")); }
    catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; result = 1; }
    CoUninitialize(); return result;
}
