#include "ImageRotation.h"
#include <cstring>
#include <limits>

std::shared_ptr<ImageData> RotateImage(const ImageData& source,const int clockwiseQuarterTurns,std::wstring& error) {
    const int turns=(clockwiseQuarterTurns%4+4)%4;
    if(!source.width || !source.height || static_cast<std::uint64_t>(source.width)*4>source.stride ||
        static_cast<std::uint64_t>(source.stride)*source.height>source.pixels.size()) {
        error=L"圖片像素資料無效。 / Invalid image pixel buffer.";return {};
    }
    try {
        auto image=std::make_shared<ImageData>();
        image->path=source.path;image->sourceWasJpegXr=source.sourceWasJpegXr;image->hdrToneMappedToSdr=source.hdrToneMappedToSdr;
        image->sourceFingerprint=source.sourceFingerprint;
        image->width=turns%2?source.height:source.width;image->height=turns%2?source.width:source.height;
        if(image->width>std::numeric_limits<std::uint32_t>::max()/4)throw std::bad_alloc();
        image->stride=image->width*4;
        const auto bytes=static_cast<std::uint64_t>(image->stride)*image->height;
        if(bytes>512ULL*1024*1024)throw std::bad_alloc();
        image->pixels.resize(static_cast<std::size_t>(bytes));
        for(std::uint32_t y=0;y<source.height;++y)for(std::uint32_t x=0;x<source.width;++x) {
            const auto dx=turns==1?source.height-1-y:turns==2?source.width-1-x:turns==3?y:x;
            const auto dy=turns==1?x:turns==2?source.height-1-y:turns==3?source.width-1-x:y;
            std::memcpy(image->pixels.data()+static_cast<std::size_t>(dy)*image->stride+dx*4,
                source.pixels.data()+static_cast<std::size_t>(y)*source.stride+x*4,4);
        }
        return image;
    }catch(...){error=L"旋轉圖片時記憶體不足或圖片過大。 / Not enough memory to rotate this image.";return {};}
}
