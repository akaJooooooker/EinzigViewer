#pragma once

#include "ImageData.h"

#include <filesystem>
#include <string>

class ImageExporter {
public:
    static bool SavePng(
        const ImageData& image,
        const std::filesystem::path& path,
        std::uint32_t maximumLongEdge,
        std::wstring& error);
};
