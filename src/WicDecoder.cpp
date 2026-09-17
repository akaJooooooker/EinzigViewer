#include "WicDecoder.h"

#include <Windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <filesystem>
#include <limits>
#include <new>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {
WICBitmapTransformOptions ReadOrientation(IWICBitmapFrameDecode* frame) {
    ComPtr<IWICMetadataQueryReader> reader;
    if (FAILED(frame->GetMetadataQueryReader(&reader))) return WICBitmapTransformRotate0;

    PROPVARIANT value;
    PropVariantInit(&value);
    const HRESULT result = reader->GetMetadataByName(L"/app1/ifd/{ushort=274}", &value);
    if (FAILED(result)) {
        PropVariantClear(&value);
        return WICBitmapTransformRotate0;
    }

    unsigned short orientation = 1;
    if (value.vt == VT_UI2) orientation = value.uiVal;
    else if (value.vt == VT_UI4) orientation = static_cast<unsigned short>(value.ulVal);
    PropVariantClear(&value);

    switch (orientation) {
    case 2: return WICBitmapTransformFlipHorizontal;
    case 3: return WICBitmapTransformRotate180;
    case 4: return WICBitmapTransformFlipVertical;
    case 5: return static_cast<WICBitmapTransformOptions>(WICBitmapTransformRotate90 | WICBitmapTransformFlipHorizontal);
    case 6: return WICBitmapTransformRotate90;
    case 7: return static_cast<WICBitmapTransformOptions>(WICBitmapTransformRotate270 | WICBitmapTransformFlipHorizontal);
    case 8: return WICBitmapTransformRotate270;
    default: return WICBitmapTransformRotate0;
    }
}

std::wstring HResultMessage(const HRESULT result) {
    wchar_t* buffer = nullptr;
    const DWORD count = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, static_cast<DWORD>(result), 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    std::wstring message = count > 0 && buffer ? std::wstring(buffer, count) : L"未知的解碼錯誤";
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

bool IsJpegXr(const std::filesystem::path& path) {
    auto extension = path.extension().wstring();
    std::ranges::transform(extension, extension.begin(), [](const wchar_t value) {
        return static_cast<wchar_t>(std::towlower(value));
    });
    return extension == L".jxr" || extension == L".wdp" || extension == L".hdp";
}

bool IsExtendedRgbFormat(const WICPixelFormatGUID& format) {
    return IsEqualGUID(format, GUID_WICPixelFormat48bppRGBFixedPoint) ||
        IsEqualGUID(format, GUID_WICPixelFormat64bppRGBAFixedPoint) ||
        IsEqualGUID(format, GUID_WICPixelFormat96bppRGBFixedPoint) ||
        IsEqualGUID(format, GUID_WICPixelFormat96bppRGBFloat) ||
        IsEqualGUID(format, GUID_WICPixelFormat128bppRGBAFloat) ||
        IsEqualGUID(format, GUID_WICPixelFormat128bppRGBFloat) ||
        IsEqualGUID(format, GUID_WICPixelFormat128bppRGBAFixedPoint) ||
        IsEqualGUID(format, GUID_WICPixelFormat64bppRGBAHalf) ||
        IsEqualGUID(format, GUID_WICPixelFormat64bppRGBHalf);
}

bool ValidateDimensions(
    const UINT width, const UINT height, const std::uint64_t bytesPerPixel,
    std::uint64_t& stride, std::uint64_t& size, HRESULT& result) {
    stride = static_cast<std::uint64_t>(width) * bytesPerPixel;
    constexpr std::uint64_t maximumDecodeBytes = 512ULL * 1024 * 1024;
    if (width == 0 || height == 0 ||
        stride > maximumDecodeBytes || height > maximumDecodeBytes / stride) {
        result = WINCODEC_ERR_IMAGESIZEOUTOFRANGE;
        return false;
    }
    size = stride * static_cast<std::uint64_t>(height);
    return true;
}

float SafeChannel(const float value) {
    return std::isfinite(value) ? std::max(0.0F, value) : 0.0F;
}

float AcesFitted(const float value) {
    const float x = std::max(0.0F, value);
    return std::clamp(
        (x * (2.51F * x + 0.03F)) / (x * (2.43F * x + 0.59F) + 0.14F), 0.0F, 1.0F);
}

float LinearToSrgb(const float value) {
    const float x = std::clamp(value, 0.0F, 1.0F);
    return x <= 0.0031308F ? 12.92F * x : 1.055F * std::pow(x, 1.0F / 2.4F) - 0.055F;
}

BYTE ToPremultipliedByte(const float linear, const float alpha) {
    return static_cast<BYTE>(std::lround(
        std::clamp(LinearToSrgb(linear) * alpha, 0.0F, 1.0F) * 255.0F));
}

HRESULT DecodeExtendedJpegXr(
    IWICImagingFactory* factory,
    IWICBitmapSource* source,
    const std::wstring& path,
    std::shared_ptr<ImageData>& image) {
    ComPtr<IWICFormatConverter> converter;
    HRESULT result = factory->CreateFormatConverter(&converter);
    if (SUCCEEDED(result)) {
        result = converter->Initialize(
            source, GUID_WICPixelFormat128bppRGBAFloat, WICBitmapDitherTypeNone,
            nullptr, 0.0, WICBitmapPaletteTypeCustom);
    }

    UINT width = 0;
    UINT height = 0;
    if (SUCCEEDED(result)) result = converter->GetSize(&width, &height);

    std::uint64_t floatStride = 0;
    std::uint64_t floatSize = 0;
    if (SUCCEEDED(result)) ValidateDimensions(width, height, 16ULL, floatStride, floatSize, result);

    std::vector<float> linearPixels;
    if (SUCCEEDED(result)) {
        try {
            linearPixels.resize(static_cast<std::size_t>(width) * height * 4ULL);
            result = converter->CopyPixels(
                nullptr, static_cast<UINT>(floatStride), static_cast<UINT>(floatSize),
                reinterpret_cast<BYTE*>(linearPixels.data()));
        } catch (const std::bad_alloc&) {
            result = E_OUTOFMEMORY;
        }
    }
    if (FAILED(result)) return result;

    float maximumLinear = 0.0F;
    for (std::size_t offset = 0; offset < linearPixels.size(); offset += 4) {
        maximumLinear = std::max({maximumLinear,
            SafeChannel(linearPixels[offset]),
            SafeChannel(linearPixels[offset + 1]),
            SafeChannel(linearPixels[offset + 2])});
    }
    const bool toneMapHdr = maximumLinear > 1.25F;

    std::uint64_t outputStride = 0;
    std::uint64_t outputSize = 0;
    if (!ValidateDimensions(width, height, 4ULL, outputStride, outputSize, result)) return result;

    try {
        image = std::make_shared<ImageData>();
        image->path = path;
        image->width = width;
        image->height = height;
        image->stride = static_cast<std::uint32_t>(outputStride);
        image->sourceWasJpegXr = true;
        image->hdrToneMappedToSdr = toneMapHdr;
        image->pixels.resize(static_cast<std::size_t>(outputSize));

        for (std::size_t pixel = 0; pixel < static_cast<std::size_t>(width) * height; ++pixel) {
            const std::size_t sourceOffset = pixel * 4ULL;
            float red = SafeChannel(linearPixels[sourceOffset]);
            float green = SafeChannel(linearPixels[sourceOffset + 1]);
            float blue = SafeChannel(linearPixels[sourceOffset + 2]);
            const float alpha = std::clamp(
                std::isfinite(linearPixels[sourceOffset + 3]) ? linearPixels[sourceOffset + 3] : 1.0F,
                0.0F, 1.0F);

            if (toneMapHdr) {
                constexpr float scRgbToSdrExposure = 80.0F / 203.0F;
                const float luminance = 0.2126F * red + 0.7152F * green + 0.0722F * blue;
                if (luminance > 0.000001F) {
                    const float mappedLuminance = AcesFitted(luminance * scRgbToSdrExposure);
                    const float ratio = mappedLuminance / luminance;
                    red *= ratio;
                    green *= ratio;
                    blue *= ratio;
                }
            }

            const float maximumChannel = std::max({red, green, blue});
            if (maximumChannel > 1.0F) {
                red /= maximumChannel;
                green /= maximumChannel;
                blue /= maximumChannel;
            }

            const std::size_t destinationOffset = pixel * 4ULL;
            image->pixels[destinationOffset] = static_cast<std::byte>(ToPremultipliedByte(blue, alpha));
            image->pixels[destinationOffset + 1] = static_cast<std::byte>(ToPremultipliedByte(green, alpha));
            image->pixels[destinationOffset + 2] = static_cast<std::byte>(ToPremultipliedByte(red, alpha));
            image->pixels[destinationOffset + 3] = static_cast<std::byte>(
                static_cast<BYTE>(std::lround(alpha * 255.0F)));
        }
    } catch (const std::bad_alloc&) {
        image.reset();
        return E_OUTOFMEMORY;
    }
    return S_OK;
}

HRESULT DecodeStandard(
    IWICImagingFactory* factory,
    IWICBitmapSource* source,
    const std::wstring& path,
    const bool jpegXr,
    std::shared_ptr<ImageData>& image) {
    ComPtr<IWICFormatConverter> converter;
    HRESULT result = factory->CreateFormatConverter(&converter);
    if (SUCCEEDED(result)) {
        result = converter->Initialize(
            source, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone,
            nullptr, 0.0, WICBitmapPaletteTypeCustom);
    }

    UINT width = 0;
    UINT height = 0;
    if (SUCCEEDED(result)) result = converter->GetSize(&width, &height);

    std::uint64_t stride = 0;
    std::uint64_t size = 0;
    if (SUCCEEDED(result)) ValidateDimensions(width, height, 4ULL, stride, size, result);

    if (SUCCEEDED(result)) {
        try {
            image = std::make_shared<ImageData>();
            image->path = path;
            image->width = width;
            image->height = height;
            image->stride = static_cast<std::uint32_t>(stride);
            image->sourceWasJpegXr = jpegXr;
            image->pixels.resize(static_cast<std::size_t>(size));
            result = converter->CopyPixels(
                nullptr, image->stride, static_cast<UINT>(image->pixels.size()),
                reinterpret_cast<BYTE*>(image->pixels.data()));
        } catch (const std::bad_alloc&) {
            result = E_OUTOFMEMORY;
        }
    }
    return result;
}
}

std::shared_ptr<ImageData> WicDecoder::Decode(const std::wstring& path, std::wstring& error) {
    error.clear();
    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    struct ComScope { HRESULT result; ~ComScope() { if (SUCCEEDED(result)) CoUninitialize(); } } scope{comResult};

    ComPtr<IWICImagingFactory> factory;
    HRESULT result = CreateFactory(factory);

    ComPtr<IWICBitmapDecoder> decoder;
    if (SUCCEEDED(result)) {
        result = factory->CreateDecoderFromFilename(
            path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder);
    }

    ComPtr<IWICBitmapFrameDecode> frame;
    if (SUCCEEDED(result)) result = decoder->GetFrame(0, &frame);

    WICPixelFormatGUID sourceFormat{};
    if (SUCCEEDED(result)) result = frame->GetPixelFormat(&sourceFormat);

    ComPtr<IWICBitmapSource> source = frame;
    ComPtr<IWICBitmapFlipRotator> rotator;
    if (SUCCEEDED(result)) {
        const auto orientation = ReadOrientation(frame.Get());
        if (orientation != WICBitmapTransformRotate0) {
            result = factory->CreateBitmapFlipRotator(&rotator);
            if (SUCCEEDED(result)) {
                result = rotator->Initialize(frame.Get(), orientation);
                source = rotator;
            }
        }
    }

    std::shared_ptr<ImageData> image;
    const bool jpegXr = IsJpegXr(path);
    if (SUCCEEDED(result) && jpegXr && IsExtendedRgbFormat(sourceFormat)) {
        result = DecodeExtendedJpegXr(factory.Get(), source.Get(), path, image);
    } else if (SUCCEEDED(result)) {
        result = DecodeStandard(factory.Get(), source.Get(), path, jpegXr, image);
    }

    if (FAILED(result)) {
        image.reset();
        error = HResultMessage(result);
    }
    return image;
}
