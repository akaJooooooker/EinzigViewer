#include "RtxHdrPresenter.h"

#include "RtxHdrProcessor.h"

#include <dxgi1_6.h>

#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <exception>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>

using Microsoft::WRL::ComPtr;

namespace {
constexpr DXGI_COLOR_SPACE_TYPE HdrColorSpace =
    DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
constexpr LONG MaximumHdrImageDimension = 7680;

bool SameRectangle(const RECT& left, const RECT& right) noexcept {
    return left.left == right.left && left.top == right.top &&
        left.right == right.right && left.bottom == right.bottom;
}

RtxHdrSettings SettingsFor(
    const HdrPreset preset, const unsigned int maxLuminance) noexcept {
    RtxHdrSettings settings;
    settings.maxLuminance = maxLuminance;
    if (preset == HdrPreset::Vivid) {
        settings.contrast = 110;
        settings.saturation = 120;
        settings.middleGray = 55;
    } else if (preset == HdrPreset::Gentle) {
        settings.contrast = 92;
        settings.saturation = 95;
        settings.middleGray = 45;
    }
    return settings;
}
}

struct RtxHdrPresenter::RenderCore final : std::enable_shared_from_this<RenderCore> {
    struct WorkItem {
        std::shared_ptr<ImageData> image;
        UINT width = 0;
        UINT height = 0;
        RECT destination{};
        HdrPreset preset = HdrPreset::Standard;
        RtxHdrSettings settings;
        std::uint64_t generation = 0;
    };

    struct Result {
        ComPtr<ID3D11Texture2D> texture;
        std::wstring path;
        std::wstring error;
        RECT destination{};
        HdrPreset preset = HdrPreset::Standard;
        std::uint64_t generation = 0;
        bool usedVsr = false;
    };

    void Start(const HWND window) {
        notificationWindow = window;
        started = true;
        const auto self = shared_from_this();
        std::thread([self] {
            self->WorkerLoop();
            if (self->retirement) self->retirement->store(true, std::memory_order_release);
        }).detach();
    }

    void Request(WorkItem work) {
        {
            std::scoped_lock lock(mutex);
            if (stopping) return;
            pending = std::move(work);
        }
        condition.notify_one();
    }

    bool TakeResult(Result& result) {
        std::scoped_lock lock(mutex);
        if (!completed) return false;
        result = std::move(*completed);
        completed.reset();
        return true;
    }

    void Stop() noexcept {
        {
            std::scoped_lock lock(mutex);
            stopping = true;
            pending.reset();
            completed.reset();
            notificationWindow = nullptr;
        }
        if (!started && retirement) retirement->store(true, std::memory_order_release);
        condition.notify_all();
    }

    RtxHdrProcessor processor;
    std::shared_ptr<std::atomic_bool> retirement;

private:
    void WorkerLoop() {
        for (;;) {
            WorkItem work;
            {
                std::unique_lock lock(mutex);
                condition.wait(lock, [&] { return stopping || pending.has_value(); });
                if (stopping) return;
                work = std::move(*pending);
                pending.reset();
            }

            Result result;
            result.path = work.image ? work.image->path : std::wstring{};
            result.destination = work.destination;
            result.preset = work.preset;
            result.generation = work.generation;
            try {
                const RECT output{0, 0,
                    static_cast<LONG>(work.width), static_cast<LONG>(work.height)};
                if (!work.image || !processor.Process(
                        *work.image, work.width, work.height, output, work.settings,
                        result.texture, result.error, true, &result.usedVsr)) {
                    if (result.error.empty()) {
                        result.error = L"HDR 背景處理失敗。 / Background HDR processing failed.";
                    }
                }
            } catch (const std::exception& exception) {
                result.error = L"HDR 背景處理發生例外，Eidolyx 將安全返回一般顯示。 / "
                               L"Background HDR processing raised an exception; Eidolyx will safely return to normal display. ";
                const std::string details = exception.what();
                result.error.append(details.begin(), details.end());
            } catch (...) {
                result.error = L"HDR 背景處理發生未知錯誤，Eidolyx 將安全返回一般顯示。 / "
                               L"Background HDR processing failed unexpectedly; Eidolyx will safely return to normal display.";
            }

            HWND notify = nullptr;
            {
                std::scoped_lock lock(mutex);
                if (stopping) return;
                completed = std::move(result);
                notify = notificationWindow;
            }
            if (notify && IsWindow(notify)) {
                PostMessageW(notify, RtxHdrPresenter::FrameReadyMessage, 0, 0);
            }
        }
    }

    HWND notificationWindow = nullptr;
    std::mutex mutex;
    std::condition_variable condition;
    std::optional<WorkItem> pending;
    std::optional<Result> completed;
    bool stopping = false;
    bool started = false;
};

RtxHdrPresenter::~RtxHdrPresenter() {
    (void)Shutdown();
}

bool RtxHdrPresenter::Initialize(const HWND window, std::wstring& error) {
    (void)Shutdown();
    window_ = window;
    if (!window_ || !IsWindow(window_)) {
        error = L"HDR 顯示缺少有效的主視窗。 / HDR presentation has no valid main window.";
        return false;
    }
    if (!RefreshHdrMonitor(error)) {
        (void)Shutdown();
        return false;
    }
    core_ = std::make_shared<RenderCore>();
    retirement_ = std::make_shared<std::atomic_bool>(false);
    core_->retirement = retirement_;
    if (!core_->processor.Initialize(error) || !CreateSwapChain(error)) {
        (void)Shutdown();
        return false;
    }
    core_->Start(window_);
    return true;
}

bool RtxHdrPresenter::RefreshHdrMonitor(std::wstring& error) {
    const HMONITOR current = MonitorFromWindow(window_, MONITOR_DEFAULTTONEAREST);
    ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
        error = L"無法查詢 HDR 顯示器。 / Could not query HDR displays.";
        return false;
    }

    for (UINT adapterIndex = 0;; ++adapterIndex) {
        ComPtr<IDXGIAdapter1> adapter;
        if (factory->EnumAdapters1(adapterIndex, &adapter) == DXGI_ERROR_NOT_FOUND) break;
        for (UINT outputIndex = 0;; ++outputIndex) {
            ComPtr<IDXGIOutput> output;
            if (adapter->EnumOutputs(outputIndex, &output) == DXGI_ERROR_NOT_FOUND) break;
            DXGI_OUTPUT_DESC outputDescription{};
            if (FAILED(output->GetDesc(&outputDescription)) ||
                outputDescription.Monitor != current) continue;

            ComPtr<IDXGIOutput6> output6;
            DXGI_OUTPUT_DESC1 description{};
            if (FAILED(output.As(&output6)) || FAILED(output6->GetDesc1(&description)) ||
                description.ColorSpace != HdrColorSpace) {
                error = L"目前的 Eidolyx 視窗不在已啟用 Windows HDR 的螢幕上。\n"
                        L"請開啟該螢幕的 HDR，或把視窗移到 HDR 螢幕後再試一次。 / "
                        L"The Eidolyx window is not on a display with Windows HDR enabled. "
                        L"Enable HDR for that display or move the window to an HDR display and try again.";
                return false;
            }

            monitor_ = current;
            monitorName_ = outputDescription.DeviceName;
            const float reported = description.MaxLuminance;
            maxLuminance_ = static_cast<unsigned int>(std::clamp(
                reported > 0.0F ? std::lround(reported) : 1000L, 400L, 2000L));
            return true;
        }
    }

    error = L"找不到目前視窗所在的 HDR 顯示器。 / "
            L"The HDR display containing the current window could not be found.";
    return false;
}

bool RtxHdrPresenter::CreateSwapChain(std::wstring& error) {
    if (!core_) return false;
    ComPtr<IDXGIDevice> dxgiDevice;
    ComPtr<IDXGIAdapter> adapter;
    ComPtr<IDXGIFactory2> factory;
    HRESULT result = core_->processor.Device()->QueryInterface(IID_PPV_ARGS(&dxgiDevice));
    if (SUCCEEDED(result)) result = dxgiDevice->GetAdapter(&adapter);
    if (SUCCEEDED(result)) result = adapter->GetParent(IID_PPV_ARGS(&factory));
    if (FAILED(result)) {
        error = L"無法取得 HDR DXGI factory。 / Could not obtain the HDR DXGI factory.";
        return false;
    }

    RECT client{};
    GetClientRect(window_, &client);
    DXGI_SWAP_CHAIN_DESC1 description{};
    description.Width = static_cast<UINT>(std::max(1L, client.right - client.left));
    description.Height = static_cast<UINT>(std::max(1L, client.bottom - client.top));
    description.Format = DXGI_FORMAT_R10G10B10A2_UNORM;
    description.SampleDesc.Count = 1;
    description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    description.BufferCount = 2;
    description.Scaling = DXGI_SCALING_STRETCH;
    description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    description.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

    ComPtr<IDXGISwapChain1> swapChain1;
    result = factory->CreateSwapChainForHwnd(
        core_->processor.Device(), window_, &description, nullptr, nullptr, &swapChain1);
    if (SUCCEEDED(result)) result = swapChain1.As(&swapChain_);
    if (FAILED(result)) {
        error = L"無法在 Eidolyx 主視窗建立 10-bit HDR swap chain。 / "
                L"Could not create the 10-bit HDR swap chain in the Eidolyx main window.";
        return false;
    }
    factory->MakeWindowAssociation(window_, DXGI_MWA_NO_ALT_ENTER);

    UINT support = 0;
    result = swapChain_->CheckColorSpaceSupport(HdrColorSpace, &support);
    if (FAILED(result) || (support & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT) == 0 ||
        FAILED(swapChain_->SetColorSpace1(HdrColorSpace))) {
        error = L"目前螢幕或 Windows 顯示模式不接受 HDR10 / Rec.2020。 / "
                L"The current display mode does not accept HDR10 / Rec.2020.";
        swapChain_.Reset();
        return false;
    }
    return true;
}

bool RtxHdrPresenter::ResizeSwapChain(
    const UINT width, const UINT height, std::wstring& error) {
    DXGI_SWAP_CHAIN_DESC1 description{};
    if (FAILED(swapChain_->GetDesc1(&description))) {
        error = L"無法讀取 HDR swap chain 狀態。 / Could not read the HDR swap-chain state.";
        return false;
    }
    if (description.Width == width && description.Height == height) return true;

    Invalidate();
    HRESULT result = swapChain_->ResizeBuffers(
        2, width, height, DXGI_FORMAT_R10G10B10A2_UNORM, 0);
    if (FAILED(result)) {
        error = L"調整主視窗 HDR swap chain 大小失敗。 / "
                L"Failed to resize the main-window HDR swap chain.";
        return false;
    }
    if (FAILED(swapChain_->SetColorSpace1(HdrColorSpace))) {
        error = L"重新設定 HDR10 色彩空間失敗。 / Failed to restore the HDR10 color space.";
        return false;
    }
    return true;
}

bool RtxHdrPresenter::ClearAndPresent(std::wstring& error) {
    if (!core_) return false;
    ComPtr<ID3D11Texture2D> backBuffer;
    HRESULT result = swapChain_->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    ComPtr<ID3D11RenderTargetView> view;
    if (SUCCEEDED(result)) {
        result = core_->processor.Device()->CreateRenderTargetView(backBuffer.Get(), nullptr, &view);
    }
    if (FAILED(result)) {
        error = L"無法取得 HDR back buffer。 / Could not obtain the HDR back buffer.";
        return false;
    }
    constexpr float black[]{0.0F, 0.0F, 0.0F, 1.0F};
    core_->processor.Context()->ClearRenderTargetView(view.Get(), black);
    if (FAILED(swapChain_->Present(1, 0))) {
        error = L"呈現 HDR 畫面失敗。 / Failed to present the HDR frame.";
        return false;
    }
    return true;
}

bool RtxHdrPresenter::Render(std::shared_ptr<ImageData> image, const RECT& destination,
    const HdrPreset preset, bool& usedVsr, std::wstring& error) {
    error.clear();
    usedVsr = false;
    if (!swapChain_ || !core_ || !window_) {
        error = L"HDR 主視窗呈現器尚未初始化。 / The main-window HDR presenter is not initialized.";
        return false;
    }

    if (MonitorFromWindow(window_, MONITOR_DEFAULTTONEAREST) != monitor_) {
        if (!RefreshHdrMonitor(error)) return false;
        Invalidate();
    }

    RECT client{};
    GetClientRect(window_, &client);
    const UINT clientWidth = static_cast<UINT>(std::max(1L, client.right - client.left));
    const UINT clientHeight = static_cast<UINT>(std::max(1L, client.bottom - client.top));
    if (!ResizeSwapChain(clientWidth, clientHeight, error)) return false;

    RenderCore::Result completed;
    if (core_->TakeResult(completed) && completed.generation == requestGeneration_) {
        if (!completed.texture) {
            error = completed.error.empty()
                ? L"HDR 背景處理沒有產生畫面。 / Background HDR processing produced no frame."
                : completed.error;
            return false;
        }
        hdrTexture_ = std::move(completed.texture);
        displayedPath_ = std::move(completed.path);
        renderedDestination_ = completed.destination;
        renderedUsedVsr_ = completed.usedVsr;
    }

    if (!image || image->pixels.empty()) {
        Invalidate();
        return ClearAndPresent(error);
    }

    const LONG imageWidth = std::max(1L, destination.right - destination.left);
    const LONG imageHeight = std::max(1L, destination.bottom - destination.top);
    if (imageWidth > MaximumHdrImageDimension || imageHeight > MaximumHdrImageDimension) {
        error = L"HDR 顯示尺寸超過 7680 像素上限；請縮小圖片後再試一次。 / "
                L"The HDR presentation exceeds the 7680-pixel limit; zoom out and try again.";
        return false;
    }

    const bool needsRequest = requestedPath_ != image->path ||
        !SameRectangle(requestedDestination_, destination) || requestedPreset_ != preset ||
        requestedClientWidth_ != clientWidth || requestedClientHeight_ != clientHeight;
    if (needsRequest) {
        requestedPath_ = image->path;
        requestedDestination_ = destination;
        requestedPreset_ = preset;
        requestedClientWidth_ = clientWidth;
        requestedClientHeight_ = clientHeight;
        const std::uint64_t generation = ++requestGeneration_;
        RenderCore::WorkItem work;
        work.image = std::move(image);
        work.width = static_cast<UINT>(imageWidth);
        work.height = static_cast<UINT>(imageHeight);
        work.destination = destination;
        work.preset = preset;
        work.settings = SettingsFor(preset, maxLuminance_);
        work.generation = generation;
        core_->Request(std::move(work));
    }

    if (!hdrTexture_) return ClearAndPresent(error);
    usedVsr = renderedUsedVsr_;

    ComPtr<ID3D11Texture2D> backBuffer;
    HRESULT result = swapChain_->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    ComPtr<ID3D11RenderTargetView> view;
    if (SUCCEEDED(result)) {
        result = core_->processor.Device()->CreateRenderTargetView(backBuffer.Get(), nullptr, &view);
    }
    if (FAILED(result)) {
        error = L"無法取得 HDR back buffer。 / Could not obtain the HDR back buffer.";
        return false;
    }
    constexpr float black[]{0.0F, 0.0F, 0.0F, 1.0F};
    core_->processor.Context()->ClearRenderTargetView(view.Get(), black);

    D3D11_TEXTURE2D_DESC textureDescription{};
    hdrTexture_->GetDesc(&textureDescription);
    const LONG visibleLeft = std::max(0L, renderedDestination_.left);
    const LONG visibleTop = std::max(0L, renderedDestination_.top);
    const LONG visibleRight = std::min(static_cast<LONG>(clientWidth), renderedDestination_.right);
    const LONG visibleBottom = std::min(static_cast<LONG>(clientHeight), renderedDestination_.bottom);
    if (visibleRight > visibleLeft && visibleBottom > visibleTop) {
        D3D11_BOX sourceBox{};
        sourceBox.left = std::min<UINT>(textureDescription.Width,
            static_cast<UINT>(visibleLeft - renderedDestination_.left));
        sourceBox.top = std::min<UINT>(textureDescription.Height,
            static_cast<UINT>(visibleTop - renderedDestination_.top));
        sourceBox.front = 0;
        sourceBox.right = std::min<UINT>(textureDescription.Width,
            static_cast<UINT>(visibleRight - renderedDestination_.left));
        sourceBox.bottom = std::min<UINT>(textureDescription.Height,
            static_cast<UINT>(visibleBottom - renderedDestination_.top));
        sourceBox.back = 1;
        if (sourceBox.right > sourceBox.left && sourceBox.bottom > sourceBox.top) {
            core_->processor.Context()->CopySubresourceRegion(
                backBuffer.Get(), 0, static_cast<UINT>(visibleLeft),
                static_cast<UINT>(visibleTop), 0, hdrTexture_.Get(), 0, &sourceBox);
        }
    }

    if (FAILED(swapChain_->Present(1, 0))) {
        error = L"呈現 HDR 畫面失敗。 / Failed to present the HDR frame.";
        return false;
    }
    return true;
}

void RtxHdrPresenter::Invalidate() noexcept {
    hdrTexture_.Reset();
    requestedPath_.clear();
    displayedPath_.clear();
    requestedDestination_ = {};
    renderedDestination_ = {};
    requestedClientWidth_ = 0;
    requestedClientHeight_ = 0;
    renderedUsedVsr_ = false;
    ++requestGeneration_;
}

std::shared_ptr<std::atomic_bool> RtxHdrPresenter::Shutdown() noexcept {
    auto retirement = retirement_;
    if (!retirement) {
        retirement = std::make_shared<std::atomic_bool>(true);
    }
    Invalidate();
    swapChain_.Reset();
    if (core_) core_->Stop();
    core_.reset();
    retirement_.reset();
    window_ = nullptr;
    monitor_ = nullptr;
    monitorName_.clear();
    maxLuminance_ = 1000;
    return retirement;
}
