#include "ImageClipboard.h"

#include <wincodec.h>
#include <wrl/client.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {
int failures = 0;

void Expect(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << "CLIPBOARD_PROBE_FAILED: " << message << '\n';
        ++failures;
    }
}

ImageData Fixture() {
    ImageData image;
    image.width = 3;
    image.height = 2;
    image.stride = 16;
    // Premultiplied BGRA, with deliberately nonzero source-row padding.
    constexpr std::array<unsigned char, 32> pixels{
        255, 0, 0, 255, 0, 0, 128, 128, 10, 20, 30, 85, 222, 222, 222, 222,
        0, 0, 0, 0, 0, 255, 0, 255, 17, 34, 51, 255, 111, 111, 111, 111};
    image.pixels.resize(pixels.size());
    std::memcpy(image.pixels.data(), pixels.data(), pixels.size());
    return image;
}

void CheckPng(const std::vector<std::byte>& png, const UINT expectedWidth, const UINT expectedHeight,
    const std::vector<unsigned char>& expected) {
    constexpr std::array<unsigned char, 8> signature{137, 80, 78, 71, 13, 10, 26, 10};
    Expect(png.size() >= signature.size() &&
        std::memcmp(png.data(), signature.data(), signature.size()) == 0,
        "PNG must use the registered PNG clipboard format's complete file payload");
    if (png.empty() || png.size() > std::numeric_limits<DWORD>::max()) return;

    ComPtr<IWICImagingFactory> factory;
    HRESULT result = CoCreateInstance(
        CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    ComPtr<IWICStream> stream;
    if (SUCCEEDED(result)) result = factory->CreateStream(&stream);
    if (SUCCEEDED(result)) result = stream->InitializeFromMemory(
        const_cast<BYTE*>(reinterpret_cast<const BYTE*>(png.data())), static_cast<DWORD>(png.size()));
    ComPtr<IWICBitmapDecoder> decoder;
    if (SUCCEEDED(result)) result = factory->CreateDecoderFromStream(
        stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, &decoder);
    ComPtr<IWICBitmapFrameDecode> frame;
    if (SUCCEEDED(result)) result = decoder->GetFrame(0, &frame);
    UINT width = 0;
    UINT height = 0;
    if (SUCCEEDED(result)) result = frame->GetSize(&width, &height);
    Expect(SUCCEEDED(result) && width == expectedWidth && height == expectedHeight,
        "PNG memory decoder must recover the source dimensions");
    ComPtr<IWICFormatConverter> converter;
    if (SUCCEEDED(result)) result = factory->CreateFormatConverter(&converter);
    if (SUCCEEDED(result)) result = converter->Initialize(
        frame.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone,
        nullptr, 0.0, WICBitmapPaletteTypeCustom);
    std::vector<unsigned char> pixels(expected.size());
    if (SUCCEEDED(result)) result = converter->CopyPixels(
        nullptr, expectedWidth * 4U, static_cast<UINT>(pixels.size()), pixels.data());
    Expect(SUCCEEDED(result) && pixels == expected,
        "PNG must preserve source orientation/color and encode straight alpha, excluding row padding");
}

void CheckMultiBlockPng() {
    // An incompressible opaque image exercises multiple PNG chunks and stream
    // growth. Dropping/truncating a write or using source padding as pixels fails.
    ImageData image;
    image.width = 257;
    image.height = 129;
    image.stride = 1032;
    image.pixels.resize(static_cast<std::size_t>(image.stride) * image.height, std::byte{211});
    std::vector<unsigned char> expected;
    expected.reserve(static_cast<std::size_t>(image.width) * image.height * 4U);
    std::uint32_t random = 0x31415926;
    for (std::uint32_t y = 0; y < image.height; ++y) {
        for (std::uint32_t x = 0; x < image.width; ++x) {
            const std::size_t offset = static_cast<std::size_t>(y) * image.stride + x * 4U;
            for (std::size_t channel = 0; channel < 3; ++channel) {
                random ^= random << 13;
                random ^= random >> 17;
                random ^= random << 5;
                const auto color = static_cast<unsigned char>(random & 255U);
                image.pixels[offset + channel] = static_cast<std::byte>(color);
                expected.push_back(color);
            }
            image.pixels[offset + 3U] = std::byte{255};
            expected.push_back(255);
        }
    }
    image_clipboard::EncodedImage encoded;
    std::wstring error;
    const bool success = image_clipboard::Encode(image, encoded, error);
    Expect(success && error.empty(), "multichunk PNG must encode successfully");
    if (!success) return;
    Expect(encoded.png.size() > 65536, "noisy fixture must exercise PNG output beyond one 64 KiB block");
    CheckPng(encoded.png, image.width, image.height, expected);
}

void CheckDib(const std::vector<std::byte>& dib) {
    Expect(dib.size() == 64, "CF_DIB must contain a 40-byte header plus two 12-byte rows");
    if (dib.size() != 64) return;
    BITMAPINFOHEADER header{};
    std::memcpy(&header, dib.data(), sizeof(header));
    Expect(header.biSize == 40 && header.biWidth == 3 && header.biHeight == 2 &&
        header.biPlanes == 1 && header.biBitCount == 24 && header.biCompression == BI_RGB &&
        header.biSizeImage == 24 && header.biClrUsed == 0 && header.biClrImportant == 0,
        "CF_DIB must use a standard positive-height 24bpp BI_RGB header, without a BMP file header");
    constexpr std::array<unsigned char, 24> expected{
        255, 255, 255, 0, 255, 0, 17, 34, 51, 0, 0, 0,
        255, 0, 0, 127, 127, 255, 180, 190, 200, 0, 0, 0};
    Expect(std::memcmp(dib.data() + 40, expected.data(), expected.size()) == 0,
        "CF_DIB must flatten premultiplied alpha onto white, reverse row order and zero DWORD padding");
}

void RejectInvalid(const ImageData& image, const char* message) {
    image_clipboard::EncodedImage encoded{{std::byte{1}}, {std::byte{2}}};
    std::wstring error;
    Expect(!image_clipboard::Encode(image, encoded, error), message);
    Expect(!error.empty(), "invalid image must report an error");
    Expect(encoded.png.empty() && encoded.dib.empty(), "failed encoding must not expose stale or partial payloads");
}

void CheckValidation() {
    RejectInvalid({}, "empty image must fail");
    auto image = Fixture();
    image.width = 0;
    RejectInvalid(image, "zero width must fail");
    image = Fixture();
    image.height = 0;
    RejectInvalid(image, "zero height must fail");
    image = Fixture();
    image.stride = 11;
    RejectInvalid(image, "stride shorter than a BGRA row must fail");
    image = Fixture();
    image.pixels.resize(27);
    RejectInvalid(image, "truncated final pixel must fail without out-of-bounds reads");
    image = Fixture();
    image.width = std::numeric_limits<std::uint32_t>::max();
    RejectInvalid(image, "dimension/row-byte overflow must fail before allocation");
    image = Fixture();
    image.height = std::numeric_limits<std::uint32_t>::max();
    RejectInvalid(image, "height outside signed DIB limits must fail before allocation");
    image = Fixture();
    image.stride = std::numeric_limits<std::uint32_t>::max();
    RejectInvalid(image, "excessive padded input storage must fail before allocation");
    image = {};
    image.width = 65536;
    image.height = 1025;
    image.stride = 262144;
    RejectInvalid(image, "input above 256 MiB must fail before attempting to encode");
}
}

int wmain() {
    // This executable never calls Copy, OpenClipboard, EmptyClipboard or SetClipboardData.
    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(comResult)) {
        std::cerr << "CLIPBOARD_PROBE_FAILED: COM initialization\n";
        return 1;
    }
    {
        image_clipboard::EncodedImage encoded;
        std::wstring error = L"stale error";
        const bool success = image_clipboard::Encode(Fixture(), encoded, error);
        Expect(success, "a valid padded premultiplied BGRA image must encode");
        if (!success) std::wcerr << error << L'\n';
        Expect(success && error.empty(), "successful encoding must clear an earlier error");
        if (success) {
            CheckPng(encoded.png, 3, 2, {
                255, 0, 0, 255, 0, 0, 255, 128, 30, 60, 90, 85,
                0, 0, 0, 0, 0, 255, 0, 255, 17, 34, 51, 255});
            CheckDib(encoded.dib);
        }
        CheckMultiBlockPng();
        CheckValidation();
    }
    CoUninitialize();
    if (failures != 0) return 2;
    std::cout << "CLIPBOARD_PROBE_OK: PNG alpha/colors/orientation; CF_DIB header/white alpha/padding; input bounds\n";
    return 0;
}
