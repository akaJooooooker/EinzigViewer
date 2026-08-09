#include "RtxHdrProcessor.h"

#include <Windows.h>
#include <dxgi1_6.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>

namespace {
ImageData CreateTestImage() {
    ImageData image;
    image.path = L"EinzigViewer TrueHDR probe";
    image.width = 640;
    image.height = 360;
    image.stride = image.width * 4U;
    image.pixels.resize(static_cast<std::size_t>(image.stride) * image.height);
    for (std::uint32_t y = 0; y < image.height; ++y) {
        for (std::uint32_t x = 0; x < image.width; ++x) {
            const std::size_t offset = static_cast<std::size_t>(y) * image.stride + x * 4U;
            const std::uint8_t red = static_cast<std::uint8_t>(x * 255U / image.width);
            const std::uint8_t green = static_cast<std::uint8_t>(y * 255U / image.height);
            const std::uint8_t blue = static_cast<std::uint8_t>((x + y) * 255U / (image.width + image.height));
            image.pixels[offset + 0] = static_cast<std::byte>(blue);
            image.pixels[offset + 1] = static_cast<std::byte>(green);
            image.pixels[offset + 2] = static_cast<std::byte>(red);
            image.pixels[offset + 3] = static_cast<std::byte>(255U);
        }
    }
    return image;
}
}

int wmain() {
    SetConsoleOutputCP(CP_UTF8);
    RtxHdrProcessor processor;
    std::wstring error;
    if (!processor.Initialize(error)) {
        std::wcerr << L"HDR_PROBE_FAILED\n"
                   << L"ERROR_LENGTH=" << error.size() << L'\n'
                   << L"ERROR=" << error << L'\n';
        return 1;
    }

    ImageData input = CreateTestImage();
    Microsoft::WRL::ComPtr<ID3D11Texture2D> output;
    RtxHdrSettings settings;
    constexpr std::uint32_t outputWidth = 1280;
    constexpr std::uint32_t outputHeight = 720;
    const RECT rectangle{0, 0, static_cast<LONG>(outputWidth), static_cast<LONG>(outputHeight)};
    bool usedVsr = false;
    if (!processor.Process(
            input, outputWidth, outputHeight, rectangle, settings,
            output, error, true, &usedVsr)) {
        std::wcerr << L"HDR_PROBE_FAILED: " << error << L'\n';
        return 2;
    }
    if (!usedVsr) {
        std::wcerr << L"HDR_PROBE_FAILED: integrated pipeline did not execute VSR.\n";
        return 5;
    }

    D3D11_TEXTURE2D_DESC description{};
    output->GetDesc(&description);
    D3D11_TEXTURE2D_DESC stagingDescription = description;
    stagingDescription.Usage = D3D11_USAGE_STAGING;
    stagingDescription.BindFlags = 0;
    stagingDescription.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
    HRESULT hr = processor.Device()->CreateTexture2D(&stagingDescription, nullptr, &staging);
    if (FAILED(hr)) return 3;
    processor.Context()->CopyResource(staging.Get(), output.Get());

    D3D11_MAPPED_SUBRESOURCE mapped{};
    hr = processor.Context()->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) return 4;
    std::uint64_t hash = 1469598103934665603ULL;
    std::uint32_t minimumChannel = std::numeric_limits<std::uint32_t>::max();
    std::uint32_t maximumChannel = 0;
    for (std::uint32_t y = 0; y < description.Height; ++y) {
        const auto* row = reinterpret_cast<const std::uint32_t*>(
            static_cast<const std::byte*>(mapped.pData) + static_cast<std::size_t>(y) * mapped.RowPitch);
        for (std::uint32_t x = 0; x < description.Width; ++x) {
            const std::uint32_t pixel = row[x];
            hash ^= pixel;
            hash *= 1099511628211ULL;
            minimumChannel = std::min({minimumChannel, pixel & 0x3FFU, (pixel >> 10U) & 0x3FFU, (pixel >> 20U) & 0x3FFU});
            maximumChannel = std::max({maximumChannel, pixel & 0x3FFU, (pixel >> 10U) & 0x3FFU, (pixel >> 20U) & 0x3FFU});
        }
    }
    processor.Context()->Unmap(staging.Get(), 0);

    unsigned int hdrOutputCount = 0;
    Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
    if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
        for (UINT adapterIndex = 0;; ++adapterIndex) {
            Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
            if (factory->EnumAdapters1(adapterIndex, &adapter) == DXGI_ERROR_NOT_FOUND) break;
            for (UINT outputIndex = 0;; ++outputIndex) {
                Microsoft::WRL::ComPtr<IDXGIOutput> display;
                if (adapter->EnumOutputs(outputIndex, &display) == DXGI_ERROR_NOT_FOUND) break;
                DXGI_OUTPUT_DESC outputDescription{};
                Microsoft::WRL::ComPtr<IDXGIOutput6> display6;
                DXGI_OUTPUT_DESC1 outputDetails{};
                if (FAILED(display->GetDesc(&outputDescription)) ||
                    FAILED(display.As(&display6)) || FAILED(display6->GetDesc1(&outputDetails))) continue;
                if (outputDetails.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020) {
                    ++hdrOutputCount;
                    std::wcout << L"HDR_OUTPUT=" << outputDescription.DeviceName
                               << L", MAX_LUMINANCE=" << outputDetails.MaxLuminance << L" nits\n";
                }
            }
        }
    }

    std::wcout << L"HDR_PROBE_OK\n"
               << L"ADAPTER=" << processor.AdapterName() << L'\n'
               << L"INPUT=640x360 BGRA8 Rec.709 SDR\n"
               << L"PIPELINE=VSR Ultra -> TrueHDR\n"
               << L"OUTPUT=1280x720 R10G10B10A2 HDR10\n"
               << L"CHANNEL_RANGE=" << minimumChannel << L".." << maximumChannel << L'\n'
               << L"WINDOWS_HDR_OUTPUTS=" << std::dec << hdrOutputCount << L'\n'
               << L"PIXEL_HASH=0x" << std::hex << std::uppercase << hash << L'\n';
    return 0;
}
