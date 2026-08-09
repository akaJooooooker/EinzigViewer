#include "RtxImageEnhancer.h"

#include <Windows.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string>

namespace {
ImageData CreateTestImage() {
    ImageData image;
    image.path = L"Eidolyx RTX probe";
    image.width = 960;
    image.height = 540;
    image.stride = image.width * 4U;
    image.pixels.resize(static_cast<std::size_t>(image.stride) * image.height);

    for (std::uint32_t y = 0; y < image.height; ++y) {
        for (std::uint32_t x = 0; x < image.width; ++x) {
            const std::size_t offset = static_cast<std::size_t>(y) * image.stride + x * 4U;
            const bool checker = ((x / 16U) + (y / 16U)) % 2U == 0U;
            const bool diagonal = ((x + y * 2U) % 47U) < 3U;
            image.pixels[offset + 0] = static_cast<std::byte>(diagonal ? 245U : (checker ? 35U : 80U));
            image.pixels[offset + 1] = static_cast<std::byte>(diagonal ? 245U : (x * 255U / image.width));
            image.pixels[offset + 2] = static_cast<std::byte>(diagonal ? 245U : (y * 255U / image.height));
            image.pixels[offset + 3] = static_cast<std::byte>(255U);
        }
    }
    return image;
}

std::uint64_t HashPixels(const ImageData& image) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (const std::byte value : image.pixels) {
        hash ^= static_cast<std::uint8_t>(value);
        hash *= 1099511628211ULL;
    }
    return hash;
}
}

int wmain() {
    SetConsoleOutputCP(CP_UTF8);
    RtxImageEnhancer enhancer;
    ImageData input = CreateTestImage();
    std::wstring error;

    const auto started = std::chrono::steady_clock::now();
    auto output = enhancer.Enhance(input, 3840, 2160, error);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started);

    if (!output) {
        std::wcerr << L"RTX_PROBE_FAILED: " << error << L'\n';
        std::wcerr << L"STATUS: " << enhancer.Status() << L'\n';
        return 1;
    }
    if (output->width != 3840 || output->height != 2160 ||
        output->pixels.size() != 3840ULL * 2160ULL * 4ULL) {
        std::wcerr << L"RTX_PROBE_FAILED: unexpected output dimensions\n";
        return 2;
    }

    std::wcout << L"RTX_PROBE_OK\n"
               << L"INPUT=960x540\n"
               << L"OUTPUT=" << output->width << L'x' << output->height << L'\n'
               << L"ELAPSED_MS=" << elapsed.count() << L'\n'
               << L"PIXEL_HASH=0x" << std::hex << std::uppercase << HashPixels(*output) << L'\n'
               << L"STATUS=" << enhancer.Status() << L'\n';
    return 0;
}
