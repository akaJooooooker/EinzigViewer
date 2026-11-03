#pragma once

#include "ImageData.h"

#include <Windows.h>

#include <cstddef>
#include <string>
#include <vector>

namespace image_clipboard {
struct EncodedImage {
    std::vector<std::byte> png;
    std::vector<std::byte> dib;
};

// Pure serialization; never opens or changes the Windows clipboard.
bool Encode(const ImageData& image, EncodedImage& encoded, std::wstring& error);

// The caller supplies its live window as the clipboard owner.
bool Copy(HWND owner, const ImageData& image, std::wstring& error);
}
