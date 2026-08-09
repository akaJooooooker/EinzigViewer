#include "ImageExporter.h"
#include "WicDecoder.h"

#include <Windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {
HRESULT CreateFactory(ComPtr<IWICImagingFactory>& factory) {
    HRESULT result = CoCreateInstance(
        CLSID_WICImagingFactory2, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (FAILED(result)) {
        result = CoCreateInstance(
            CLSID_WICImagingFactory1, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    }
    return result;
}

bool CreateHdrJpegXr(const std::filesystem::path& path, std::wstring& error) {
    constexpr UINT width = 256;
    constexpr UINT height = 128;
    std::vector<float> pixels(static_cast<std::size_t>(width) * height * 4ULL);
    for (UINT y = 0; y < height; ++y) {
        for (UINT x = 0; x < width; ++x) {
            const std::size_t offset = (static_cast<std::size_t>(y) * width + x) * 4ULL;
            const float horizontal = static_cast<float>(x) / static_cast<float>(width - 1);
            const float vertical = static_cast<float>(y) / static_cast<float>(height - 1);
            pixels[offset] = horizontal * 12.0F;
            pixels[offset + 1] = 0.05F + vertical * 5.0F;
            pixels[offset + 2] = 0.02F + (1.0F - horizontal) * 2.5F;
            pixels[offset + 3] = 1.0F;
        }
    }

    ComPtr<IWICImagingFactory> factory;
    HRESULT result = CreateFactory(factory);
    ComPtr<IWICStream> stream;
    if (SUCCEEDED(result)) result = factory->CreateStream(&stream);
    if (SUCCEEDED(result)) result = stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE);
    ComPtr<IWICBitmapEncoder> encoder;
    if (SUCCEEDED(result)) result = factory->CreateEncoder(GUID_ContainerFormatWmp, nullptr, &encoder);
    if (SUCCEEDED(result)) result = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> options;
    if (SUCCEEDED(result)) result = encoder->CreateNewFrame(&frame, &options);
    if (SUCCEEDED(result)) result = frame->Initialize(options.Get());
    if (SUCCEEDED(result)) result = frame->SetSize(width, height);
    WICPixelFormatGUID format = GUID_WICPixelFormat128bppRGBAFloat;
    if (SUCCEEDED(result)) result = frame->SetPixelFormat(&format);
    if (SUCCEEDED(result) && !IsEqualGUID(format, GUID_WICPixelFormat128bppRGBAFloat)) {
        result = WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT;
    }
    if (SUCCEEDED(result)) {
        result = frame->WritePixels(
            height, width * 16U, static_cast<UINT>(pixels.size() * sizeof(float)),
            reinterpret_cast<BYTE*>(pixels.data()));
    }
    if (SUCCEEDED(result)) result = frame->Commit();
    if (SUCCEEDED(result)) result = encoder->Commit();
    if (FAILED(result)) {
        error = L"Synthetic JPEG XR encode failed: 0x" + std::to_wstring(static_cast<unsigned long>(result));
        return false;
    }
    return true;
}
}

int wmain(const int argumentCount, wchar_t** arguments) {
    SetConsoleOutputCP(CP_UTF8);
    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(comResult)) return 1;

    const std::filesystem::path outputDirectory = argumentCount > 1
        ? std::filesystem::path(arguments[1])
        : std::filesystem::temp_directory_path() / L"EinzigViewerImageIoProbe";
    std::error_code filesystemError;
    std::filesystem::create_directories(outputDirectory, filesystemError);
    if (filesystemError) {
        std::wcerr << L"IMAGE_IO_PROBE_FAILED: could not create output directory\n";
        CoUninitialize();
        return 2;
    }

    const auto jxrPath = outputDirectory / L"synthetic-hdr.jxr";
    const auto pngPath = outputDirectory / L"synthetic-hdr-sdr.png";
    const auto smallPath = outputDirectory / L"synthetic-hdr-sdr-small.png";
    std::wstring error;
    if (!CreateHdrJpegXr(jxrPath, error)) {
        std::wcerr << L"IMAGE_IO_PROBE_FAILED: " << error << L'\n';
        CoUninitialize();
        return 3;
    }

    auto decoded = WicDecoder::Decode(jxrPath.wstring(), error);
    if (!decoded || decoded->width != 256 || decoded->height != 128 ||
        !decoded->sourceWasJpegXr || !decoded->hdrToneMappedToSdr) {
        std::wcerr << L"IMAGE_IO_PROBE_FAILED: HDR JPEG XR decode/tone map failed: " << error << L'\n';
        CoUninitialize();
        return 4;
    }
    if (!ImageExporter::SavePng(*decoded, pngPath, 0, error) ||
        !ImageExporter::SavePng(*decoded, smallPath, 64, error)) {
        std::wcerr << L"IMAGE_IO_PROBE_FAILED: PNG export failed: " << error << L'\n';
        CoUninitialize();
        return 5;
    }

    auto fullPng = WicDecoder::Decode(pngPath.wstring(), error);
    auto smallPng = WicDecoder::Decode(smallPath.wstring(), error);
    if (!fullPng || !smallPng || fullPng->width != 256 || fullPng->height != 128 ||
        smallPng->width != 64 || smallPng->height != 32 ||
        fullPng->sourceWasJpegXr || smallPng->sourceWasJpegXr) {
        std::wcerr << L"IMAGE_IO_PROBE_FAILED: exported PNG dimensions are incorrect: " << error << L'\n';
        CoUninitialize();
        return 6;
    }

    const auto [minimum, maximum] = std::minmax_element(decoded->pixels.begin(), decoded->pixels.end());
    if (minimum == decoded->pixels.end() || maximum == decoded->pixels.end() || *minimum == *maximum) {
        std::wcerr << L"IMAGE_IO_PROBE_FAILED: tone-mapped image has no useful range\n";
        CoUninitialize();
        return 7;
    }

    std::wcout << L"IMAGE_IO_PROBE_OK\n"
               << L"JXR=" << jxrPath << L"\n"
               << L"PNG=" << pngPath << L"\n"
               << L"FULL_SIZE=" << fullPng->width << L'x' << fullPng->height << L"\n"
               << L"SMALL_SIZE=" << smallPng->width << L'x' << smallPng->height << L"\n"
               << L"HDR_TONE_MAPPED=" << (decoded->hdrToneMappedToSdr ? L"YES" : L"NO") << L'\n';
    CoUninitialize();
    return 0;
}
