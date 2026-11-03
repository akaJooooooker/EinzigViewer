#pragma once
#include "ImageData.h"
#include <Windows.h>
#include <memory>
#include <vector>

namespace image_composition {
struct Bounds { double left=0,top=0,right=0,bottom=0; };
struct Layer { std::shared_ptr<const ImageData> image; Bounds bounds; };
// Bounds and selection use physical client pixels. Output is opaque PBGRA,
// containing image content and the viewer background, never desktop pixels.
std::shared_ptr<ImageData> Compose(const std::vector<Layer>& layers,
    RECT selection,RECT viewport,std::wstring& error);
}
