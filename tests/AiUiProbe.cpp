#include "App.h"
#include "ImageExporter.h"
#include "../resources/AiResourceDialogIds.h"
#include <commctrl.h>
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <thread>

// Hidden window; fake inference changes pixels distinctly and blocks on a gate.
struct AiUiTest {
    static std::string Utf8(const std::wstring& value) {
        const auto bytes=WideCharToMultiByte(CP_UTF8,0,value.data(),static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);
        std::string result(bytes,'\0');
        if(bytes)WideCharToMultiByte(CP_UTF8,0,value.data(),static_cast<int>(value.size()),result.data(),bytes,nullptr,nullptr);
        return result;
    }
    struct AvailableEnhancer final:ImageEnhancer {
        bool IsAvailable() const noexcept override{return true;}
        std::wstring Name() const override{return L"Controlled RTX";}
        std::wstring Status() const override{return {};}
        std::shared_ptr<ImageData> Enhance(const ImageData& source,std::uint32_t,std::uint32_t,std::wstring&) override{
            return std::make_shared<ImageData>(source);
        }
    };
    static void Require(bool value,const char* message) { if(!value)throw std::runtime_error(message); }
    template<class Predicate> static void Await(App& app,Predicate ready,const char* message) {
        const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(15);
        while(!ready() && std::chrono::steady_clock::now()<until) {
            MSG msg{};while(PeekMessageW(&msg,app.window_,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        Require(ready(),message);
    }
    static ai::ResourceSnapshot Healthy() {
        constexpr std::uint64_t gib=1024ULL*1024*1024;
        ai::ResourceSnapshot snapshot;snapshot.memoryKnown=snapshot.diskKnown=snapshot.gpuKnown=true;
        snapshot.totalPhysical=64*gib;snapshot.availablePhysical=40*gib;snapshot.availableCommit=40*gib;
        snapshot.diskTotal=1000*gib;snapshot.diskAvailable=250*gib;snapshot.gpuTotal=16*gib;snapshot.gpuAvailable=8*gib;
        return snapshot;
    }
    static inline bool resourceDialogSeen=false;
    static inline bool resourceDialogApply=false;
    static inline bool resourceDialogTimedOut=false;
    static inline ULONGLONG resourceDialogDeadline=0;
    static LRESULT CALLBACK ResourceDialogSubclass(HWND dialog,UINT message,WPARAM wParam,LPARAM lParam,
        UINT_PTR id,DWORD_PTR) {
        if(message==WM_TIMER && wParam==5902) {
            if(GetTickCount64()>=resourceDialogDeadline) {
                resourceDialogTimedOut=true;
                wchar_t error[512]{};GetDlgItemTextW(dialog,IDC_AI_ERROR,error,512);
                std::cerr<<"RESOURCE_DIALOG_TIMEOUT apply="<<resourceDialogApply<<" error="<<Utf8(error)<<"\n";
                PostMessageW(dialog,WM_COMMAND,IDCANCEL,0);
            }else if(resourceDialogApply && IsWindowEnabled(GetDlgItem(dialog,IDC_AI_REFRESH))) {
                resourceDialogApply=false;
                SendMessageW(GetDlgItem(dialog,IDC_AI_MANUAL),BM_CLICK,0,0);
                SetDlgItemTextW(dialog,IDC_AI_RAM,L"16");SetDlgItemTextW(dialog,IDC_AI_DISK,L"24");
                SetDlgItemTextW(dialog,IDC_AI_CPU,L"37");SetDlgItemTextW(dialog,IDC_AI_BATCH,L"2");
                SetDlgItemTextW(dialog,IDC_AI_GPU,L"0");
                PostMessageW(dialog,WM_COMMAND,IDOK,0);
            }
            return 0;
        }
        if(message==WM_NCDESTROY) {
            KillTimer(dialog,5902);RemoveWindowSubclass(dialog,ResourceDialogSubclass,id);
        }
        return DefSubclassProc(dialog,message,wParam,lParam);
    }
    static LRESULT CALLBACK ResourceDialogHook(int code,WPARAM wParam,LPARAM lParam) {
        if(code>=0) {
            const auto message=reinterpret_cast<CWPRETSTRUCT*>(lParam);
            if(message->message==WM_INITDIALOG && GetDlgItem(message->hwnd,IDC_AI_RAM)) {
                resourceDialogSeen=true;
                resourceDialogDeadline=GetTickCount64()+6000;
                SetWindowSubclass(message->hwnd,ResourceDialogSubclass,1,0);
                SetTimer(message->hwnd,5902,15,nullptr);
                if(!resourceDialogApply)PostMessageW(message->hwnd,WM_COMMAND,IDCANCEL,0);
            }
        }
        return CallNextHookEx(nullptr,code,wParam,lParam);
    }
    static void ResourceSettingsRun(const std::filesystem::path& fixtures) {
        // Earlier test App windows posted WM_QUIT when they were destroyed.
        // A real app opens its dialog before shutdown, so remove those fixture
        // shutdown messages before entering the native modal message loop.
        MSG quit{};while(PeekMessageW(&quit,nullptr,WM_QUIT,WM_QUIT,PM_REMOVE)){}
        constexpr std::uint64_t gib=1024ULL*1024*1024;
        const auto root=fixtures/L"ai-resource-ui";std::filesystem::create_directories(root);
        ImageData fixture;fixture.width=24;fixture.height=32;fixture.stride=96;fixture.pixels.resize(96*32,std::byte{255});
        std::wstring fixtureError;
        for(int i=1;i<=3;++i)Require(ImageExporter::SavePng(fixture,root/(std::to_wstring(i)+L".png"),0,fixtureError),"create resource UI pages");
        auto probes=std::make_shared<std::atomic_int>(0),calls=std::make_shared<std::atomic_int>(0);
        App app;app.settingsPathForTest_=root/L"settings.ini";
        app.aiResourceProbeForTest_=[probes](const auto&,int){++*probes;return Healthy();};
        app.aiExecutorForTest_=[calls](const ai::RuntimeOptions& options,const ImageData& source,int scale,const auto&,
            const std::atomic_bool&,std::wstring&)->ai::Output {
            Require(options.cpuPercent==37 && options.jobMemoryBytes==8ULL*1024*1024*1024,"manual CPU/RAM reaches Manager runtime options");
            ++*calls;auto image=std::make_shared<ImageData>();image->width=source.width*scale;image->height=source.height*scale;
            image->stride=image->width*4;image->pixels.resize(static_cast<std::size_t>(image->stride)*image->height,std::byte{255});
            return {image,{}};
        };
        Require(app.CreateDeviceIndependentResources() && app.CreateMainWindow(GetModuleHandleW(nullptr)),"create resource settings window");
        struct WindowScope{App& app;~WindowScope(){if(IsWindow(app.window_))DestroyWindow(app.window_);}} scope{app};
        app.mainMenu_=app.CreateApplicationMenu();SetMenu(app.window_,app.mainMenu_);
        Require(!app.aiEnabled_ && !app.aiManager_ && probes->load()==0,"closed UI with AI off never probes or creates manager");
        resourceDialogSeen=false;resourceDialogTimedOut=false;
        auto hook=SetWindowsHookExW(WH_CALLWNDPROCRET,ResourceDialogHook,nullptr,GetCurrentThreadId());
        Require(hook!=nullptr,"install own-thread resource cancel hook");
        app.HandleMessage(WM_COMMAND,GetMenuItemID(app.aiMenu_,5),0);UnhookWindowsHookEx(hook);
        Require(resourceDialogSeen && !resourceDialogTimedOut && !app.aiEnabled_ && !app.aiManager_ && calls->load()==0,"resource dialog opens with no document and never enables manager or inference");
        Await(app,[&]{return probes->load()==1;},"resource dialog one-shot probe completes after cancel");
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        Require(probes->load()==1,"closed resource dialog does not keep sampling while AI is off");
        ai::UserLimits limits;limits.manual=true;limits.ramBytes=16*gib;limits.diskBytes=24*gib;limits.cpuPercent=37;
        resourceDialogApply=true;
        hook=SetWindowsHookExW(WH_CALLWNDPROCRET,ResourceDialogHook,nullptr,GetCurrentThreadId());
        Require(hook!=nullptr,"install own-thread resource apply hook");
        app.HandleMessage(WM_COMMAND,GetMenuItemID(app.aiMenu_,5),0);UnhookWindowsHookEx(hook);
        if(resourceDialogApply || !app.aiUserLimits_.manual || app.aiUserLimits_.ramBytes!=16*gib || app.aiUserLimits_.cpuPercent!=37 || app.aiManager_)
            std::cerr<<"RESOURCE_APPLY pending="<<resourceDialogApply<<" manual="<<app.aiUserLimits_.manual<<" RAM="<<app.aiUserLimits_.ramBytes
                <<" CPU="<<app.aiUserLimits_.cpuPercent<<" manager="<<static_cast<bool>(app.aiManager_)<<" notice="<<Utf8(app.notice_)<<"\n";
        Require(!resourceDialogTimedOut && !resourceDialogApply && app.aiUserLimits_.manual && app.aiUserLimits_.ramBytes==16*gib && app.aiUserLimits_.cpuPercent==37 && !app.aiManager_,
            "native Apply saves valid resource controls without enabling AI");
        const auto reloaded=ai::ReadUserLimits(app.settingsPathForTest_);
        Require(reloaded.manual && reloaded.ramBytes==16*gib && reloaded.cpuPercent==37,"App apply persists exact limits");
        app.aiUserLimits_={};app.LoadSettings();
        Require(app.aiUserLimits_.manual && app.aiUserLimits_.cpuPercent==37 && !app.aiEnabled_ && !app.aiManager_,"settings load restores limits with AI off");
        const auto goodPath=app.settingsPathForTest_;app.settingsPathForTest_=root/L"directory-is-not-ini";
        std::filesystem::create_directories(app.settingsPathForTest_);
        auto changed=limits;changed.cpuPercent=62;
        Require(!app.ApplyAiResourceSettings(changed) && app.aiUserLimits_.cpuPercent==37 && !app.notice_.empty(),"failed save does not silently apply new limits");
        app.settingsPathForTest_=goodPath;
        WritePrivateProfileStringW(L"aiResources",L"cpuPercent",L"broken",goodPath.c_str());
        app.LoadSettings();Require(!app.aiUserLimits_.error.empty(),"corrupt manual settings loaded as invalid");
        Require(app.OpenImage(root/L"1.png"),"open source for invalid saved settings");
        Await(app,[&]{return app.currentImage_!=nullptr;},"decode resource source");
        app.ToggleAi();Require(!app.aiEnabled_ && !app.aiManager_ && !app.notice_.empty() && calls->load()==0,"invalid persisted manual settings block inference with readable error");
        Require(app.ApplyAiResourceSettings(limits),"repair saved resource settings");app.ToggleAi();
        try { Await(app,[&]{return app.aiManager_->Status().complete==app.folder_.Size();},"manual resource AI document completes"); }
        catch(...) {
            const auto status=app.aiManager_->Status();std::cerr<<"RESOURCE_STATUS complete="<<status.complete<<" total="<<status.total<<" failed="<<status.failed
                <<" paused="<<status.paused<<" reason="<<Utf8(status.reason)<<"\n";
            for(std::size_t i=0;i<app.folder_.Size();++i)std::cerr<<"RESOURCE_PAGE "<<i<<" "<<Utf8(app.aiManager_->ErrorFor(app.AiKey(i),2))<<"\n";
            throw;
        }
        app.ApplyAiResult();const auto original=app.currentImage_;const auto cached=app.AiDisplayImage(0,original);
        Require(app.aiManager_->Status().limits.totalBytes==16*gib && app.aiManager_->Status().limits.batchPages==2,
            "App manual RAM and batch reach Manager admission policy");
        Require(cached!=original,"manual resource result displays");
        app.SetContinuous(true);app.RefreshContinuous();
        const auto position=app.scrollPosition_,width=app.ReaderWidth(),total=app.layout_.Total();const auto generation=app.aiDocumentGeneration_;
        const auto completedCalls=calls->load();
        changed=limits;changed.cpuPercent=62;changed.batchPages=1;
        const auto start=GetTickCount64();Require(app.ApplyAiResourceSettings(changed),"apply while AI active");
        Require(GetTickCount64()-start<250 && app.aiEnabled_ && app.aiDocumentGeneration_==generation && app.AiDisplayImage(0,original)==cached,
            "active apply returns promptly and keeps document generation and valid display cache");
        Await(app,[&]{return app.aiManager_->Status().limits.cpuPercent==62;},"manager adopts applied CPU limit");
        app.ApplyAiResult();Require(app.scrollPosition_==position && app.ReaderWidth()==width && app.layout_.Total()==total && calls->load()==completedCalls,
            "resource replan preserves reader geometry and completed inference cache");
        std::cout<<"PASS resource settings no-document/off entry, explicit save/reload, failed save, invalid saved settings, active apply cache and geometry\n";
    }
    static void ScaleOptionsRun(const std::filesystem::path& root) {
        const auto directory=root/L"ai-scales-ui";std::filesystem::create_directories(directory);
        ImageData image;image.width=24;image.height=32;image.stride=96;image.pixels.resize(96*32,std::byte{255});
        std::wstring error;
        for(int i=0;i<4;++i)Require(ImageExporter::SavePng(image,directory/(std::to_wstring(i)+L".png"),0,error),"create tiny scale pages");
        auto calls=std::make_shared<std::atomic_int>(0);auto largest=std::make_shared<std::atomic_size_t>(0);
        App app;app.aiResourceProbeForTest_=[](const std::filesystem::path&,int){return Healthy();};
        app.aiBatchExecutorForTest_=[calls,largest](const ai::RuntimeOptions&,const std::vector<ai::BatchInput>& batch,
            int scale,const std::filesystem::path&,const std::atomic_bool& cancelled,const ai::Completed& done,std::wstring&) {
            ++*calls;largest->store(std::max(largest->load(),batch.size()));
            for(std::size_t i=0;i<batch.size() && !cancelled.load();++i) {
                auto output=std::make_shared<ImageData>();output->width=batch[i].image->width*scale;output->height=batch[i].image->height*scale;
                output->stride=output->width*4;output->pixels.resize(static_cast<std::size_t>(output->stride)*output->height,std::byte{255});
                done(i,{output,{}});
            }
        };
        Require(app.CreateDeviceIndependentResources() && app.CreateMainWindow(GetModuleHandleW(nullptr)),"create tiny scale window");
        struct WindowScope{App& app;~WindowScope(){if(IsWindow(app.window_))DestroyWindow(app.window_);}} scope{app};
        app.mainMenu_=app.CreateApplicationMenu();SetMenu(app.window_,app.mainMenu_);
        wchar_t resourceLabel[128]{};GetMenuStringW(app.aiMenu_,5,resourceLabel,128,MF_BYPOSITION);
        Require(std::wstring(resourceLabel).find(L"資源設定")!=std::wstring::npos,
            "AI resource settings available while AI is off and no document is open");
        Require(app.OpenImage(directory/L"0.png"),"open tiny scale folder");
        Await(app,[&]{return app.currentImage_!=nullptr;},"decode tiny scale original");
        const auto original=app.currentImage_;
        for(int i=0;i<GetMenuItemCount(app.aiMenu_);++i) {
            wchar_t label[128]{};GetMenuStringW(app.aiMenu_,i,label,128,MF_BYPOSITION);
            Require(std::wstring(label).find(L"8×")==std::wstring::npos,"8x is absent from the AI menu");
        }
        app.HandleMessage(WM_COMMAND,GetMenuItemID(app.aiMenu_,3),0);
        Require(app.aiScale_==4 && (GetMenuState(app.aiMenu_,3,MF_BYPOSITION)&MF_CHECKED),"4x menu selects supported scale");
        app.SetAiScale(8);Require(app.aiScale_==4,"stale 8x selection keeps supported scale");
        app.ToggleAi();
        Await(app,[&]{return app.aiManager_->Status().complete==4;},"4x preprocesses whole current folder");app.ApplyAiResult();
        const auto result=app.AiDisplayImage(0,original);
        Require(result!=original && result->width==96 && result->height==128 &&
            app.currentImage_==original,"4x exact display dimensions preserve original");
        Require(largest->load()>1,"background UI work uses a batch for remaining pages");
        const auto modeCalls=calls->load();const auto generation=app.aiDocumentGeneration_;
        app.SetContinuous(true);
        Require(app.AiDisplayImage(0,original)==result && app.aiDisplayedPages_.size()>1,"continuous mode immediately reuses completed visible pages");
        app.SetContinuous(false);
        Require(app.aiDisplayedPages_.size()==1 && app.AiDisplayImage(0,original)==result,
            "single-page mode did not refresh AI visibility and reuse its completed current page");
        for(int cycle=0;cycle<3;++cycle) {
            app.SetContinuous(true);Require(app.AiDisplayImage(0,original)==result,"continuous round-trip lost completed AI image");
            app.SetContinuous(false);Require(app.AiDisplayImage(0,original)==result,"single-page round-trip lost completed AI image");
        }
        Require(calls->load()==modeCalls && app.aiDocumentGeneration_==generation,"mode switch re-inferred completed pages or changed document identity");
        const auto completedCalls=calls->load();app.ToggleAi();app.ToggleAi();
        Await(app,[&]{return app.AiDisplayImage(0,original)!=original;},"reenable reuses completed4x cache");
        Require(calls->load()==completedCalls,"reenabling completed document does not reinfer");
        std::cout<<"PASS 2x/4x menu, 8x rejection, complete-document batches and reactivation cache\n";
    }
    static void RealRun(const std::filesystem::path& fixture,const std::filesystem::path& runtime) {
        App app;
        app.aiUserLimits_.manual=true;app.aiUserLimits_.ramBytes=16ULL*1024*1024*1024;
        app.aiUserLimits_.diskBytes=4ULL*1024*1024*1024;app.aiUserLimits_.cpuPercent=37;
        app.aiUserLimits_.batchPages=2;app.aiUserLimits_.gpuReserveBytes=0;
        const auto results=fixture.parent_path().parent_path()/L"real-results";
        std::filesystem::create_directories(results);
        auto batches=std::make_shared<std::atomic_int>(0);
        auto largestBatch=std::make_shared<std::atomic_size_t>(0);
        app.aiBatchExecutorForTest_=[runtime,batches,largestBatch](const ai::RuntimeOptions& limits,
            const std::vector<ai::BatchInput>& inputs,int scale,const std::filesystem::path& root,
            const std::atomic_bool& cancelled,const ai::Completed& completed,std::wstring& error) {
            ++*batches;largestBatch->store(std::max(largestBatch->load(),inputs.size()));
            Require(limits.cpuPercent==37 && inputs.size()<=2,"manual CPU/batch reaches actual model runtime");
            ai::RuntimeOptions options=limits;options.executable=runtime/L"realcugan-ncnn-vulkan.exe";
            options.modelDirectory=runtime/L"models-se";
            ai::RunBatch(options,inputs,scale,root,cancelled,completed,error);
            if(!error.empty())std::cerr<<"REAL_BATCH_ERROR scale="<<scale<<" pages="<<inputs.size()<<" "<<Utf8(error)<<'\n';
        };
        Require(app.CreateDeviceIndependentResources() && app.CreateMainWindow(GetModuleHandleW(nullptr)),"create real AI window");
        struct WindowScope{App& app;~WindowScope(){if(IsWindow(app.window_))DestroyWindow(app.window_);}} scope{app};
        Require(app.OpenImage(fixture),"open real AI fixture");
        Await(app,[&]{return app.currentImage_!=nullptr;},"decode real AI fixture");
        const auto original=app.currentImage_;const auto pixels=original->pixels;
        Require(!app.aiManager_,"real AI default is inert");
        for(const int scale:{2,4}) {
            const auto batchStart=batches->load();largestBatch->store(0);
            if(scale==4)app.SetContinuous(true);
            const auto position=app.scrollPosition_,width=app.ReaderWidth();
            const auto start=std::chrono::steady_clock::now();
            app.SetAiScale(scale);if(!app.aiEnabled_)app.ToggleAi();
            Require(std::chrono::steady_clock::now()-start<std::chrono::milliseconds(250),"real AI starts asynchronously");
            Require(app.AiDisplayImage(app.folder_.Index(),original)==original,"real AI original first");
            try {
                Await(app,[&]{return app.AiDisplayImage(app.folder_.Index(),original)!=original;},"real model result through App/Manager/Runtime");
            }catch(...) {
                const auto status=app.aiManager_->Status();
                std::cerr<<"REAL_STATUS complete="<<status.complete<<" failed="<<status.failed<<" paused="<<status.paused
                    <<" reason="<<Utf8(status.reason)<<" page="<<Utf8(app.aiManager_->ErrorFor(app.AiKey(app.folder_.Index()),scale))<<'\n';
                throw;
            }
            app.ApplyAiResult();
            const auto result=app.AiDisplayImage(app.folder_.Index(),original);
            Require(result->width==original->width*scale && result->height==original->height*scale,"real AI exact dimensions");
            Require(app.currentImage_==original && original->pixels==pixels,"real inference leaves original untouched");
            std::wstring exportError;
            Require(ImageExporter::SavePng(*result,results/(L"AI-"+std::to_wstring(scale)+L"x.png"),0,exportError),"save real AI comparison fixture");
            if(app.continuous_)Require(app.scrollPosition_==position && app.ReaderWidth()==width,"real completion keeps continuous geometry");
            Require(app.EnsureRenderTarget(),"real AI render target");app.Paint();
            app.HandleMessage(WM_KEYDOWN,L'C',0);Require(app.AiDisplayImage(app.folder_.Index(),original)==original,"real compare");
            app.HandleMessage(WM_KEYUP,L'C',0);Require(app.AiDisplayImage(app.folder_.Index(),original)==result,"real compare release");
            Await(app,[&]{const auto status=app.aiManager_->Status();return status.complete==app.folder_.Size() && !status.processing;},"real whole-folder preprocessing completes and child exits");
            const auto completed=app.aiManager_->Status();
            Require(completed.limits.gpuKnown?largestBatch->load()>1:largestBatch->load()==1,
                "real batch size follows reliable GPU telemetry or conservative single-page fallback");
            const auto cachedCalls=batches->load();const auto switchStart=GetTickCount64();
            for(int cycle=0;cycle<3;++cycle) {
                app.SetContinuous(false);app.Paint();
                Require(app.AiDisplayImage(app.folder_.Index(),original)==result && app.bitmap_ &&
                    app.bitmap_->GetPixelSize().width==result->width,"real single mode reuses completed AI pixels");
                app.SetContinuous(true);app.Paint();
                Require(app.AiDisplayImage(app.folder_.Index(),original)==result,"real continuous mode reuses completed AI pixels");
            }
            Require(batches->load()==cachedCalls,"real mode round trip started another inference child");
            std::cout<<"REAL_MODE_CACHE scale="<<scale<<" six_switches_ms="<<GetTickCount64()-switchStart<<" extra_batches=0\n";
            const auto milliseconds=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count();
            std::cout<<"PASS real App/Manager/Runtime "<<scale<<"x "<<result->width<<"x"<<result->height<<" "<<milliseconds
                <<"ms pages="<<app.folder_.Size()<<" batches="<<(batches->load()-batchStart)<<" largest="<<largestBatch->load()
                <<" budget="<<completed.limits.totalBytes<<" gpuKnown="<<completed.limits.gpuKnown
                <<" telemetry="<<(completed.limits.gpuKnown?"verified/multi-page":"unverified/single-page")<<"\n";
        }
        app.ToggleAi();Require(!app.aiEnabled_ && app.aiDisplayedPages_.empty(),"real off restores original and releases display");
    }
    static void Run(const std::filesystem::path& fixtures) {
        auto calls=std::make_shared<std::atomic_int>(0);
        auto probes=std::make_shared<std::atomic_int>(0);
        auto low=std::make_shared<std::atomic_bool>(true);
        auto gate=std::make_shared<std::atomic_bool>(false);
        App app;
        app.aiResourceProbeForTest_=[probes,low](const std::filesystem::path&,int) {
            ++*probes;
            auto snapshot=Healthy();
            if(low->load())snapshot.availablePhysical=512ULL*1024*1024;
            return snapshot;
        };
        app.aiExecutorForTest_=[calls,gate](const ai::RuntimeOptions&,const ImageData& source,int scale,
            const std::filesystem::path&,const std::atomic_bool& cancelled,std::wstring&)->ai::Output {
            ++*calls;
            while(!gate->load() && !cancelled.load())std::this_thread::sleep_for(std::chrono::milliseconds(5));
            if(cancelled.load())return {};
            auto image=std::make_shared<ImageData>();image->path=source.path;
            image->width=source.width*scale;image->height=source.height*scale;image->stride=image->width*4;
            image->pixels.resize(static_cast<std::size_t>(image->stride)*image->height);
            for(std::size_t i=0;i<image->pixels.size();i+=4){image->pixels[i]=std::byte{17};image->pixels[i+1]=std::byte{37};image->pixels[i+2]=std::byte{197};image->pixels[i+3]=std::byte{255};}
            return {image,{}};
        };
        Require(app.CreateDeviceIndependentResources() && app.CreateMainWindow(GetModuleHandleW(nullptr)),"create hidden AI window");
        struct WindowScope{App& app;~WindowScope(){if(IsWindow(app.window_))DestroyWindow(app.window_);}} scope{app};
        app.mainMenu_=app.CreateApplicationMenu();SetMenu(app.window_,app.mainMenu_);
        Require(app.OpenImage(fixtures/L"001.png"),"open source");
        Await(app,[&]{return app.currentImage_!=nullptr;},"decode source");
        const auto original=app.currentImage_;
        app.SetAiScale(8);Require(app.aiScale_==2,"removed 8x scale cannot be selected");
        Require(!app.aiEnabled_ && !app.aiManager_ && calls->load()==0 && probes->load()==0,"default off creates no manager, probes or inference");
        app.enhancementEnabled_=true;app.hdrEnabled_=true;
        app.HandleMessage(WM_KEYDOWN,L'A',0);
        Require(app.aiEnabled_ && app.aiManager_,"manual toggle enables AI and creates manager");
        Require(!app.enhancementEnabled_ && !app.hdrEnabled_,"AI exits RTX VSR and HDR");
        Await(app,[&]{return app.aiManager_->Status().paused;},"low resources pause before inference");
        Require(calls->load()==0 && app.AiDisplayImage(0,original)==original &&
            app.AiStatus().find(L"暫停")!=std::wstring::npos,"resource pause retains original with visible reason");
        low->store(false);
        Await(app,[&]{return calls->load()>0;},"AI background call");
        Require(app.AiDisplayImage(0,original)==original,"processing displays original");
        gate->store(true);
        Await(app,[&]{return app.AiDisplayImage(0,original)!=original;},"AI result replaces display");
        app.ApplyAiResult();
        Require(app.currentImage_==original && app.AiDisplayImage(0,original)->width==original->width*2,"AI is display only at 2x source size");
        Await(app,[&]{return app.aiManager_->Status().complete>=2;},"non-visible pages preprocessed in current folder");
        Require(app.aiManager_->Status().total==app.folder_.Size() &&
            app.aiManager_->TryGet(app.AiKey(1),2)!=nullptr,"whole document registered and nonvisible result reusable");
        Require(app.AiStatus().find(L"/")!=std::wstring::npos,"AI status exposes document progress");
        const auto cached=app.AiDisplayImage(0,original);const auto cacheCalls=calls->load();
        app.currentImage_=std::make_shared<ImageData>(*original);app.RefreshAiRequests();
        Require(app.AiDisplayImage(0,app.currentImage_)==cached,"decoded source replacement reuses matching AI pixels");
        app.currentImage_=original;app.RefreshAiRequests();
        Require(calls->load()==cacheCalls,"warm display refresh does not reinfer");
        Require(app.EnsureRenderTarget(),"create render target");app.UploadCurrentBitmap();
        Require(app.bitmap_ && app.bitmap_->GetPixelSize().width==original->width*2,"single-page bitmap uses AI pixels");
        const auto beforeOversize=app.aiDisplayedPages_;
        auto oversized=std::make_shared<ImageData>();oversized->width=app.renderTarget_->GetMaximumBitmapSize()+1;
        oversized->height=1;oversized->stride=oversized->width*4;oversized->pixels.resize(oversized->stride);
        app.aiDisplayedPages_.at(app.AiKey(0)).image=oversized;app.UploadCurrentBitmap();
        Require(app.bitmap_ && app.bitmap_->GetPixelSize().width==original->width && app.AiDisplayImage(0,original)==original,
            "AI bitmap above device limit falls back to displayed original");
        app.aiDisplayedPages_=beforeOversize;app.UploadCurrentBitmap();
        app.HandleMessage(WM_KEYDOWN,L'C',0);
        Require(app.AiDisplayImage(0,original)==original && app.bitmap_->GetPixelSize().width==original->width,"hold C shows original");
        app.HandleMessage(WM_KEYUP,L'C',0);
        Require(app.AiDisplayImage(0,original)!=original,"release C restores AI");
        std::shared_ptr<ImageData> copied;
        app.clipboardWriterForTest_=[&](const ImageData& image,std::wstring&){copied=std::make_shared<ImageData>(image);return true;};
        app.HandleMessage(WM_COPY,0,0);
        Require(copied && copied->pixels==original->pixels,"whole-image copy remains source pixels");
        app.BeginRegionCopy();Require(app.regionSnapshot_!=nullptr,"capture AI viewport");
        bool aiPixel=false;for(std::size_t i=0;i<app.regionSnapshot_->pixels.size();i+=4)
            if(app.regionSnapshot_->pixels[i]==std::byte{17} && app.regionSnapshot_->pixels[i+2]==std::byte{197}){aiPixel=true;break;}
        Require(aiPixel,"region captures displayed AI pixels");app.CancelRegionCopy();
        gate->store(false);const auto callsBefore=calls->load();app.SetAiScale(4);
        Await(app,[&]{return calls->load()>callsBefore;},"4x requested");
        Require(app.AiDisplayImage(0,original)==original,"scale switch never displays old scale");
        app.BeginRegionCopy();const auto frozen=app.regionSnapshot_;Require(frozen!=nullptr,"freeze original while processing");
        gate->store(true);Await(app,[&]{return app.AiDisplayImage(0,original)!=original;},"4x complete during selection");
        app.ApplyAiResult();Require(app.regionSnapshot_==frozen && app.selectingRegion_,"completion preserves frozen capture");
        app.CancelRegionCopy();Require(app.AiDisplayImage(0,original)->width==original->width*4,"4x matches original size");
        app.SetContinuous(true);app.SetViewMode(ViewMode::FitWidth);
        Await(app,[&]{return app.cache_.TryGet((fixtures/L"002.png").wstring())!=nullptr;},"neighbor source decode");
        app.RefreshContinuous();
        const auto scroll=app.scrollPosition_,width=app.ReaderWidth(),total=app.layout_.Total();const auto zoom=app.readerZoom_;
        app.ApplyAiResult();app.Paint();
        Require(app.scrollPosition_==scroll && app.ReaderWidth()==width && app.layout_.Total()==total && app.readerZoom_==zoom,"AI completion preserves continuous geometry");
        const auto displayed=app.aiDisplayedPages_;
        const auto tileKey=std::tuple{app.folder_.Index(),0U,0U};
        Require(app.readerTiles_.contains(tileKey),"visible AI tile exists");
        app.aiDisplayedPages_.clear();app.Paint();
        Require(app.readerTiles_.contains(tileKey) && app.readerTiles_.at(tileKey)->GetPixelSize().width==original->width,
            "viewport display ownership change invalidates old AI tiles without completion message");
        app.aiDisplayedPages_=displayed;app.Paint();
        app.HandleMessage(WM_KEYDOWN,L'C',0);Require(app.showOriginalForComparison_,"continuous comparison key");
        app.HandleMessage(WM_KEYUP,L'C',0);Require(!app.showOriginalForComparison_,"continuous comparison release");
        app.SetContinuous(false);app.Navigate(1);Require(app.aiEnabled_,"same document navigation retains AI");
        Await(app,[&]{return app.currentImage_ && app.currentImage_->path==app.folder_.Current().wstring();},"next page source");
        app.RotateCurrentPage(1);Require(app.aiEnabled_,"rotation keeps AI on");
        Await(app,[&]{return app.AiDisplayImage(app.folder_.Index(),app.currentImage_)!=app.currentImage_;},"rotated page AI result");
        Require(app.AiDisplayImage(app.folder_.Index(),app.currentImage_)->width==app.currentImage_->width*4,"rotation never uses stale orientation");
        app.ToggleAi();Require(!app.aiEnabled_ && app.AiDisplayImage(app.folder_.Index(),app.currentImage_)==app.currentImage_,"off immediately restores original");
        app.ToggleAi();Require(app.aiEnabled_,"reenable AI");
        app.ResetDocument();Require(!app.aiEnabled_,"new document resets AI");
        Await(app,[&]{return app.currentImage_!=nullptr;},"source after reset");
        gate->store(false);const auto finalCalls=calls->load();app.ToggleAi();
        Await(app,[&]{return calls->load()>finalCalls;},"new generation background call");
        app.ToggleAi();gate->store(true);app.ApplyAiResult();
        Require(!app.aiEnabled_ && app.AiDisplayImage(app.folder_.Index(),app.currentImage_)==app.currentImage_,"cancelled late result cannot reenable AI");
        app.enhancementWorker_.reset();app.enhancer_=std::make_unique<AvailableEnhancer>();
        app.enhancementWorker_=std::make_unique<EnhancementWorker>(*app.enhancer_,app.window_);
        app.ToggleAi();app.ToggleEnhancement();
        Require(!app.aiEnabled_ && app.enhancementEnabled_,"enabling RTX disables AI");
        app.ToggleAi();Require(app.aiEnabled_ && !app.enhancementEnabled_,"enabling AI disables RTX again");
        std::cout<<"PASS: opt-in AI, original-first, 2x/4x, compare, original copy, displayed-pixel capture, frozen selection, geometry, navigation, rotation, reset, cancellation.\n";
    }
};
int wmain(int argc,wchar_t** argv) {
    std::cout.setf(std::ios::unitbuf);std::cerr.setf(std::ios::unitbuf);
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    int result=0;try{
        if(argc==4 && std::wstring(argv[1])==L"--real")AiUiTest::RealRun(std::filesystem::absolute(argv[2]),std::filesystem::absolute(argv[3]));
        else {
            const auto fixtures=std::filesystem::absolute(argc>1?argv[1]:L"build/reader-probe");
            AiUiTest::Run(fixtures);AiUiTest::ScaleOptionsRun(fixtures);AiUiTest::ResourceSettingsRun(fixtures);
        }
    }
    catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';result=1;}CoUninitialize();return result;
}
