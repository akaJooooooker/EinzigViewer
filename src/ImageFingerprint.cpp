#include "ImageFingerprint.h"
#include <Windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>
#include <vector>

std::string ComputeImageFingerprint(const ImageData& image) {
    if(!image.width || !image.height || std::uint64_t(image.width)*4>image.stride ||
        std::uint64_t(image.stride)*image.height>image.pixels.size())
        throw std::invalid_argument("Invalid image fingerprint input");
    struct Algorithm { BCRYPT_ALG_HANDLE value=nullptr; ~Algorithm(){if(value)BCryptCloseAlgorithmProvider(value,0);} } algorithm;
    const auto check=[](NTSTATUS status){if(status<0)throw std::runtime_error("Image SHA256 failed");};
    check(BCryptOpenAlgorithmProvider(&algorithm.value,BCRYPT_SHA256_ALGORITHM,nullptr,0));
    DWORD objectBytes=0,returned=0;
    check(BCryptGetProperty(algorithm.value,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&objectBytes),sizeof(objectBytes),&returned,0));
    std::vector<UCHAR> object(objectBytes);
    struct Hash { BCRYPT_HASH_HANDLE value=nullptr; ~Hash(){if(value)BCryptDestroyHash(value);} } hash;
    check(BCryptCreateHash(algorithm.value,&hash.value,object.data(),objectBytes,nullptr,0,0));
    const std::array<std::uint32_t,3> dimensions{image.width,image.height,image.stride};
    check(BCryptHashData(hash.value,reinterpret_cast<PUCHAR>(const_cast<std::uint32_t*>(dimensions.data())),sizeof(dimensions),0));
    const auto bytes=static_cast<std::size_t>(image.stride)*image.height;
    for(std::size_t offset=0;offset<bytes;) {
        const auto count=static_cast<ULONG>(std::min<std::size_t>(bytes-offset,std::numeric_limits<ULONG>::max()));
        check(BCryptHashData(hash.value,reinterpret_cast<PUCHAR>(const_cast<std::byte*>(image.pixels.data()+offset)),count,0));
        offset+=count;
    }
    std::array<UCHAR,32> digest{};
    check(BCryptFinishHash(hash.value,digest.data(),static_cast<ULONG>(digest.size()),0));
    constexpr char hex[]="0123456789abcdef";
    std::string result;result.reserve(64);
    for(const auto byte:digest){result+=hex[byte>>4];result+=hex[byte&15];}
    return result;
}
