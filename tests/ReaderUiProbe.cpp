#include "App.h"
#include "ImageExporter.h"
#include <wincodec.h>
#include <algorithm>
#include <chrono>
#include <cmath>
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
    static void TileSeams(App& app,const std::filesystem::path& fixtures) {
        const auto directory=fixtures/L"tile-seams";std::filesystem::create_directories(directory);
        ImageData original;original.width=1024;original.height=1536;original.stride=original.width*4;
        original.pixels.resize(static_cast<std::size_t>(original.stride)*original.height,std::byte{255});
        std::wstring error;const auto path=directory/L"white.png";
        Require(ImageExporter::SavePng(original,path,0,error),"write seam original");
        Require(app.OpenImage(path),"open seam original");
        Await(app,[&]{return app.currentImage_ && app.currentImage_->path==path.wstring();},"seam original decode");
        app.SetContinuous(true);app.SetViewMode(ViewMode::FitWidth);app.readerZoom_=1.07F;app.showInfo_=false;
        app.scrollPosition_=0.127;app.RefreshContinuous();
        auto enhanced=std::make_shared<ImageData>();enhanced->width=4096;enhanced->height=6144;enhanced->stride=enhanced->width*4;
        enhanced->pixels.resize(static_cast<std::size_t>(enhanced->stride)*enhanced->height,std::byte{255});
        enhanced->sourceFingerprint=app.currentImage_->sourceFingerprint;
        app.aiManager_=std::make_unique<ai::Manager>(nullptr,ai::RuntimeOptions{},directory/L"unused-ai");
        app.aiEnabled_=true;app.aiDisplayedPages_[app.AiKey(0)]={app.currentImage_,enhanced};
        const auto size=app.ReaderSize();const auto w=static_cast<UINT>(std::ceil(size.width)),h=static_cast<UINT>(std::ceil(size.height));
        Microsoft::WRL::ComPtr<IWICImagingFactory> wic;
        Require(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&wic))),"seam WIC factory");
        const auto draw=[&](bool tiled) {
            Microsoft::WRL::ComPtr<IWICBitmap> surface;
            Require(SUCCEEDED(wic->CreateBitmap(w,h,GUID_WICPixelFormat32bppPBGRA,WICBitmapCacheOnLoad,&surface)),"seam surface");
            Microsoft::WRL::ComPtr<ID2D1RenderTarget> target;
            const auto properties=D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),96,96);
            Require(SUCCEEDED(app.d2dFactory_->CreateWicBitmapRenderTarget(surface.Get(),properties,&target)),"seam offscreen target");
            target->BeginDraw();target->Clear(D2D1::ColorF(0x0B0E12));
            if(tiled) {app.readerTiles_.clear();app.readerTileSources_.clear();app.DrawContinuousInto(target.Get(),size);}
            else {
                Microsoft::WRL::ComPtr<ID2D1Bitmap> bitmap;
                Require(SUCCEEDED(target->CreateBitmap(D2D1::SizeU(enhanced->width,enhanced->height),enhanced->pixels.data(),enhanced->stride,
                    D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),96,96),&bitmap)),"seam whole-image reference");
                const auto width=app.ReaderWidth();const auto left=(size.width-width)/2;const auto top=-app.scrollPosition_*width;
                target->PushAxisAlignedClip(D2D1::RectF(0,0,size.width,size.height),D2D1_ANTIALIAS_MODE_ALIASED);
                target->DrawBitmap(bitmap.Get(),D2D1::RectF(static_cast<float>(left),static_cast<float>(top),
                    static_cast<float>(left+width),static_cast<float>(top+width*1.5)),1,D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
                target->PopAxisAlignedClip();
            }
            Require(SUCCEEDED(target->EndDraw()),"seam offscreen draw");
            std::vector<std::byte> pixels(static_cast<std::size_t>(w)*h*4);
            Require(SUCCEEDED(surface->CopyPixels(nullptr,w*4,static_cast<UINT>(pixels.size()),reinterpret_cast<BYTE*>(pixels.data()))),"seam pixel readback");
            return pixels;
        };
        const auto compare=[&] {
            const auto reference=draw(false),tiled=draw(true);
            unsigned maximum=0;std::size_t different=0;
            for(std::size_t i=0;i<reference.size();++i) {
                const auto delta=static_cast<unsigned>(std::abs(static_cast<int>(reference[i])-static_cast<int>(tiled[i])));
                maximum=std::max(maximum,delta);if(delta>2)++different;
            }
            std::cout<<"TILE_SEAMS zoom="<<app.readerZoom_<<" max="<<maximum<<" differing="<<different<<"\n";
            if(different) {
                std::map<unsigned,unsigned> rows,columns;
                for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x) {
                    const auto at=(static_cast<std::size_t>(y)*w+x)*4;
                    if(std::abs(static_cast<int>(reference[at])-static_cast<int>(tiled[at]))>2){++rows[y];++columns[x];}
                }
                unsigned printed=0;for(const auto [x,count]:columns)if(printed++<8)std::cout<<" DIFF_COL "<<x<<" count="<<count;
                printed=0;for(const auto [y,count]:rows)if(printed++<8)std::cout<<" DIFF_ROW "<<y<<" count="<<count;
                std::cout<<"\n SIZE "<<size.width<<"x"<<size.height<<" width="<<app.ReaderWidth()<<" scroll="<<app.scrollPosition_<<"\n";
            }
            Require(maximum<=2,"fractionally scaled continuous AI tiles introduce grid lines absent from the whole image");
        };
        compare();
        for(unsigned y=0;y<enhanced->height;++y)for(unsigned x=0;x<enhanced->width;++x) {
            const auto at=static_cast<std::size_t>(y)*enhanced->stride+x*4;
            const unsigned alpha=96+(x/47+y/53)%160;
            enhanced->pixels[at]=std::byte((x/17%256)*alpha/255);
            enhanced->pixels[at+1]=std::byte((y/19%256)*alpha/255);
            enhanced->pixels[at+2]=std::byte(((x+y)/23%256)*alpha/255);
            enhanced->pixels[at+3]=std::byte(alpha);
        }
        for(const float zoom:{1.07F,0.49F,0.113F}) {app.readerZoom_=zoom;compare();}
        app.readerTiles_.clear();app.readerTileSources_.clear();app.aiEnabled_=false;app.aiDisplayedPages_.clear();app.aiManager_.reset();
    }
    static void Run(const std::filesystem::path& fixtures) {
        App app;
        Require(app.CreateDeviceIndependentResources() && app.CreateMainWindow(GetModuleHandleW(nullptr)), "create hidden reader window");
        struct WindowScope { App& app; ~WindowScope() { if (IsWindow(app.window_)) DestroyWindow(app.window_); } } scope{app};
        app.mainMenu_ = app.CreateApplicationMenu(); SetMenu(app.window_, app.mainMenu_);
        Require(app.OpenImage(fixtures / L"001.png"), "open PNG");
        Await(app, [&] { return app.currentImage_ != nullptr; }, "first PNG decode");
        std::shared_ptr<ImageData> copied;
        app.clipboardWriterForTest_=[&](const ImageData& image,std::wstring&){copied=std::make_shared<ImageData>(image);return true;};
        app.HandleMessage(WM_COPY,0,0);
        Require(copied && copied->width==app.currentImage_->width && copied->pixels==app.currentImage_->pixels,
            "copy whole page preserves source resolution and pixels");
        const auto originalWidth=app.currentImage_->width,originalHeight=app.currentImage_->height;
        app.RotateCurrentPage(1);
        Require(app.currentImage_ && app.currentImage_->width==originalHeight && app.currentImage_->height==originalWidth,"right rotation uses rotated dimensions");
        app.HandleMessage(WM_COPY,0,0);
        Require(copied->width==originalHeight && copied->height==originalWidth,"copy follows per-page rotation");
        app.Navigate(1);
        Await(app,[&]{return app.currentImage_!=nullptr;},"next page after rotation");
        Require(!app.pageRotations_.contains(app.folder_.PageKey(app.folder_.Index())),"rotation is isolated to the chosen page");
        app.Navigate(-1);
        Await(app,[&]{return app.currentImage_!=nullptr;},"return to rotated page");
        Require(app.currentImage_->width==originalHeight,"rotation survives page navigation");
        app.SetContinuous(true);
        app.RotateCurrentPage(0);
        Require(app.currentImage_->width==originalWidth && app.layout_.Height(0)==static_cast<double>(originalHeight)/originalWidth,"reset restores continuous layout");
        app.SetContinuous(true);
        Require(app.continuous_ && !app.hdrEnabled_, "enable continuous mode");
        app.SetViewMode(ViewMode::FitWindow);
        Require(app.continuous_, "fit window must preserve continuous reading");
        Require(app.ReaderWidth()<=app.ReaderSize().width-24+0.01 &&
            app.layout_.Height(app.folder_.Index())*app.ReaderWidth()<=app.ReaderHeight()+0.01,
            "continuous fit window contains the whole leading page");
        app.SetViewMode(ViewMode::FitWidth);
        Require(app.continuous_ && std::abs(app.ReaderWidth()-(app.ReaderSize().width-24))<0.01,
            "continuous fit width fills available width");
        app.SetViewMode(ViewMode::ActualSize);
        const double dpi=static_cast<double>(GetDpiForWindow(app.window_))/96;
        Require(app.continuous_ && std::abs(app.ReaderWidth()-originalWidth/dpi)<0.01,"continuous actual size maps source to physical pixels");
        app.SetViewMode(ViewMode::FitWindow);
        Require(app.ReaderWidth()<=originalWidth/dpi+0.01,"fit window does not enlarge a small page");
        const auto fitWidth=app.ReaderWidth();
        app.ZoomAt(0.5F,POINT{300,200});
        Require(std::abs(app.ReaderWidth()-fitWidth*0.5)<0.01,"manual reader zoom can shrink the fitted page");
        const auto referenceWidth=app.ReaderWidth();
        app.JumpContinuous(1);
        Require(std::abs(app.ReaderWidth()-referenceWidth)<0.01,"mixed page ratios do not change zoom while scrolling");
        app.JumpContinuous(0);app.SetViewMode(ViewMode::FitWindow);
        RECT oldBounds{};GetWindowRect(app.window_,&oldBounds);
        SetWindowPos(app.window_,nullptr,0,0,oldBounds.right-oldBounds.left+160,oldBounds.bottom-oldBounds.top+100,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
        Require(app.continuous_ && app.layout_.Height(0)*app.ReaderWidth()<=app.ReaderHeight()+0.01,"resize recomputes continuous fit");
        SetWindowPos(app.window_,nullptr,0,0,oldBounds.right-oldBounds.left,oldBounds.bottom-oldBounds.top,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
        app.SetViewMode(ViewMode::FitWidth);
        Await(app, [&] { return app.cache_.TryGet((fixtures / L"003.png").wstring()) != nullptr; }, "long page prefetch");
        app.RefreshContinuous();
        Require(app.EnsureRenderTarget(), "Direct2D target");
        const auto decodedForOverlay=app.currentImage_;app.currentImage_.reset();
        app.renderTarget_->BeginDraw();app.DrawOverlay(app.renderTarget_->GetSize());app.renderTarget_->EndDraw();
        app.currentImage_=decodedForOverlay;
        app.JumpContinuous(0);
        app.ScrollContinuous(app.layout_.Top(1) * app.ReaderWidth() - app.ReaderHeight() * 0.4);
        app.Paint();
        Require(app.readerTiles_.size() >= 2, "multiple pages visible across seam");
        app.BeginRegionCopy();
        Require(app.selectingRegion_ && app.regionSnapshot_,"begin region copy from the continuous viewport");
        const auto snapshot=app.regionSnapshot_;const auto viewport=app.regionViewport_;
        app.HandleMessage(WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(120,100));
        app.HandleMessage(WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(420,300));
        app.Paint();
        app.HandleMessage(WM_LBUTTONUP,0,MAKELPARAM(420,300));
        Require(!app.selectingRegion_ && copied && copied->width==300 && copied->height==200,"drag release copies visible cross-page region");
        const auto firstPixel=static_cast<std::size_t>(100-viewport.top)*snapshot->stride+static_cast<std::size_t>(120-viewport.left)*4;
        Require(copied->pixels[0]==snapshot->pixels[firstPixel] && copied->pixels[2]==snapshot->pixels[firstPixel+2],"region pixels exclude selection outline and UI");
        const auto beforeCancel=copied;
        app.BeginRegionCopy();app.HandleMessage(WM_KEYDOWN,VK_ESCAPE,0);
        Require(!app.selectingRegion_ && copied==beforeCancel,"Esc cancels region copy without touching clipboard");
        app.BeginRegionCopy();app.HandleMessage(WM_CONTEXTMENU,0,0);
        Require(!app.selectingRegion_ && copied==beforeCancel,"right-click cancels region copy");
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
        const auto dense=fixtures/L"dense-reader";std::filesystem::create_directories(dense);
        for(unsigned i=1;i<=110;++i) {
            auto name=std::to_wstring(i);name.insert(name.begin(),3-name.size(),L'0');
            std::filesystem::copy_file(fixtures/L"001.png",dense/(name+L".png"),std::filesystem::copy_options::overwrite_existing);
        }
        Require(app.OpenImage(dense/L"001.png"),"open dense-page regression fixture");
        app.SetContinuous(true);Await(app,[&]{return app.currentImage_!=nullptr;},"dense reader first page");
        app.SetViewMode(ViewMode::FitWindow);app.ZoomAt(0.02F,POINT{400,300});app.JumpContinuous(50);
        Await(app,[&]{return app.cache_.TryGet(app.folder_.At(99).wstring())!=nullptr;},"small zoom requests the forward visible pages");
        TileSeams(app,fixtures);
        std::cout << "PASS: hidden Win32/Direct2D reader, page seam, tall-image tiles, precision wheel, Home/End, zoom, single-page transition, English menu, async EPUB navigation, source replacement, cancellation, temp cleanup.\n";
    }
};
int wmain(int argc, wchar_t** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    int result = 0;
    try { ReaderUiTest::Run(std::filesystem::absolute(argc > 1 ? argv[1] : L"build/reader-probe")); }
    catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; result = 1; }
    CoUninitialize(); return result;
}
