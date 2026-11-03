#pragma once
#include "ImageData.h"
#include <memory>
std::shared_ptr<ImageData> RotateImage(const ImageData& source,int clockwiseQuarterTurns,std::wstring& error);
