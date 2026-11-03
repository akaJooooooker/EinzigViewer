#pragma once

#include "ImageData.h"
#include <string>

// Worker-only: hashes the decoded dimensions, stride and pixel buffer.
std::string ComputeImageFingerprint(const ImageData& image);
