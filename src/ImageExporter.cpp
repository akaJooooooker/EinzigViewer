#include "ImageExporter.h"

#include <Windows.h>
#include <propvarutil.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <limits>

using Microsoft::WRL::ComPtr;

namespace {
std::wstring HResultMessage(const HRESULT result) {
    wchar_t* buffer = nullptr;
    const DWORD count = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, static_cast<DWORD>(result), 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    std::wstring message = count > 0 && buffer ? std::wstring(buffer, count) : L"未知的圖片輸出錯誤";
    if (buffer) LocalFree(buffer);
    while (!message.empty() && (message.back() == L'\r' || message.back() == L'\n')) message.pop_back();
    return message;
}

HRESULT CreateFactory(ComPtr<IWICImagingFactory>& factory) {
    HRESULT result = CoCreateInstance(
        CLSID_WICImagingFactory2, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (FAILED(result)) {
        result = CoCreateInstance(
            CLSID_WICImagingFactory1, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    }
    return result;
}
}

bool ImageExporter::SavePng(
    const ImageData& image,
    const std::filesystem::path& path,
    const std::uint32_t maximumLongEdge,
    std::wstring& error) {
    error.clear();
    if (image.width == 0 || image.height == 0 || image.stride == 0 || image.pixels.empty()) {
        error = L"圖片資料是空的。 / The image data is empty.";
        return false;
    }
    if (image.pixels.size() > std::numeric_limits<UINT>::max()) {
        error = L"圖片太大，無法由 Windows PNG 編碼器處理。 / The image is too large for the Windows PNG encoder.";
        return false;
    }

    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    struct ComScope {
        HRESULT result;
        ~ComScope() { if(SUCCEEDED(result))CoUninitialize(); }
    } comScope{comResult};

    ComPtr<IWICImagingFactory> factory;
    HRESULT result = CreateFactory(factory);

    ComPtr<IWICBitmap> bitmap;
    if (SUCCEEDED(result)) {
        result = factory->CreateBitmapFromMemory(
            image.width, image.height, GUID_WICPixelFormat32bppPBGRA,
            image.stride, static_cast<UINT>(image.pixels.size()),
            const_cast<BYTE*>(reinterpret_cast<const BYTE*>(image.pixels.data())), &bitmap);
    }

    UINT outputWidth = image.width;
    UINT outputHeight = image.height;
    const std::uint32_t sourceLongEdge = std::max(image.width, image.height);
    if (maximumLongEdge > 0 && maximumLongEdge < sourceLongEdge) {
        const double scale = static_cast<double>(maximumLongEdge) / static_cast<double>(sourceLongEdge);
        outputWidth = std::max<UINT>(1, static_cast<UINT>(std::llround(image.width * scale)));
        outputHeight = std::max<UINT>(1, static_cast<UINT>(std::llround(image.height * scale)));
    }

    ComPtr<IWICBitmapSource> source = bitmap;
    ComPtr<IWICBitmapScaler> scaler;
    if (SUCCEEDED(result) && (outputWidth != image.width || outputHeight != image.height)) {
        result = factory->CreateBitmapScaler(&scaler);
        if (SUCCEEDED(result)) {
            result = scaler->Initialize(
                source.Get(), outputWidth, outputHeight, WICBitmapInterpolationModeFant);
            source = scaler;
        }
    }

    ComPtr<IWICFormatConverter> straightAlpha;
    if (SUCCEEDED(result)) result = factory->CreateFormatConverter(&straightAlpha);
    if (SUCCEEDED(result)) {
        result = straightAlpha->Initialize(
            source.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone,
            nullptr, 0.0, WICBitmapPaletteTypeCustom);
        source = straightAlpha;
    }

    ComPtr<IWICStream> stream;
    if (SUCCEEDED(result)) result = factory->CreateStream(&stream);
    if (SUCCEEDED(result)) result = stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE);

    ComPtr<IWICBitmapEncoder> encoder;
    if (SUCCEEDED(result)) {
        result = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
    }
    if (SUCCEEDED(result)) result = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);

    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> options;
    if (SUCCEEDED(result)) result = encoder->CreateNewFrame(&frame, &options);
    if (SUCCEEDED(result)) result = frame->Initialize(options.Get());
    if (SUCCEEDED(result)) result = frame->SetSize(outputWidth, outputHeight);
    if (SUCCEEDED(result)) result = frame->SetResolution(96.0, 96.0);

    WICPixelFormatGUID pixelFormat = GUID_WICPixelFormat32bppBGRA;
    if (SUCCEEDED(result)) result = frame->SetPixelFormat(&pixelFormat);
    if (SUCCEEDED(result) && !IsEqualGUID(pixelFormat, GUID_WICPixelFormat32bppBGRA)) {
        result = WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT;
    }

    if (SUCCEEDED(result)) {
        ComPtr<IWICMetadataQueryWriter> metadata;
        if (SUCCEEDED(frame->GetMetadataQueryWriter(&metadata))) {
            PROPVARIANT intent;
            PropVariantInit(&intent);
            intent.vt = VT_UI1;
            intent.bVal = 0;
            (void)metadata->SetMetadataByName(L"/sRGB/RenderingIntent", &intent);
            PropVariantClear(&intent);
        }
        result = frame->WriteSource(source.Get(), nullptr);
    }
    if (SUCCEEDED(result)) result = frame->Commit();
    if (SUCCEEDED(result)) result = encoder->Commit();

    if (FAILED(result)) error = HResultMessage(result);
    return SUCCEEDED(result);
}
