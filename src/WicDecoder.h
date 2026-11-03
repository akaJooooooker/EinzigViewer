#pragma once

#include "ImageData.h"

#include <memory>
#include <cstdint>
#include <string>

class WicDecoder {
public:
    static std::shared_ptr<ImageData> Decode(const std::wstring& path, std::wstring& error,
        std::uint64_t maximumDecodeBytes = 512ULL * 1024 * 1024);
};

