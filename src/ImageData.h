#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct ImageData {
    std::wstring path;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t stride = 0;
    std::vector<std::byte> pixels;
    // Identity of the decoded original, retained by rotations and AI outputs.
    std::string sourceFingerprint;
    bool sourceWasJpegXr = false;
    bool hdrToneMappedToSdr = false;

    [[nodiscard]] std::size_t ByteSize() const noexcept {
        return pixels.size();
    }
};

