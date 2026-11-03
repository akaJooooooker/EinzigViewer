#include "ImageComposition.h"
#include <iostream>
#include <stdexcept>

namespace {
void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
std::shared_ptr<ImageData> Solid(unsigned w,unsigned h,unsigned b,unsigned g,unsigned r,unsigned a=255) {
    auto image=std::make_shared<ImageData>();image->width=w;image->height=h;image->stride=w*4+4;
    image->pixels.resize(static_cast<std::size_t>(image->stride)*h);
    for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x) {
        const unsigned channels[]{b,g,r,a};
        for(unsigned c=0;c<4;++c)image->pixels[y*image->stride+x*4+c]=static_cast<std::byte>(channels[c]);
    }
    return image;
}
unsigned Pixel(const ImageData& image,unsigned x,unsigned y,unsigned channel) {
    return std::to_integer<unsigned>(image.pixels[y*image.stride+x*4+channel]);
}
}
int main(){try {
    auto red=Solid(2,2,0,0,255),blue=Solid(2,2,255,0,0);
    const std::vector<image_composition::Layer> pages{{red,{1,0,3,2}},{blue,{1,2,3,4}}};
    std::wstring error;
    auto seam=image_composition::Compose(pages,{0,1,4,3},{0,0,4,4},error);
    Require(seam && seam->width==4 && seam->height==2,"compose cross-page selection");
    Require(Pixel(*seam,1,0,2)==255 && Pixel(*seam,1,1,0)==255,"exact pixels across page seam");
    Require(Pixel(*seam,0,0,0)==0x12 && Pixel(*seam,0,0,1)==0x0e && Pixel(*seam,0,0,2)==0x0b,"viewer background outside pages");
    auto reversed=image_composition::Compose(pages,{9,9,-2,-2},{1,1,3,3},error);
    Require(reversed && reversed->width==2 && reversed->height==2,"reverse drag is clamped to image viewport");
    auto transparent=Solid(1,1,0,0,64,128);
    auto blended=image_composition::Compose({{transparent,{0,0,1,1}}},{0,0,1,1},{0,0,1,1},error);
    Require(blended && Pixel(*blended,0,0,2)==69 && Pixel(*blended,0,0,3)==255,"premultiplied alpha over viewer background");
    auto scaled=image_composition::Compose({{red,{0,0,4,4}}},{1,1,3,3},{0,0,4,4},error);
    Require(scaled && Pixel(*scaled,0,0,2)==255 && scaled->pixels.size()==16,"scaled image region uses display resolution");
    Require(!image_composition::Compose(pages,{0,0,0,2},{0,0,4,4},error),"empty selection rejected");
    Require(!image_composition::Compose(pages,{0,0,100000,100000},{0,0,100000,100000},error),"oversized crop rejected before allocation");
    auto malformed=Solid(1,1,0,0,0);malformed->pixels.clear();
    Require(!image_composition::Compose({{malformed,{0,0,1,1}}},{0,0,1,1},{0,0,1,1},error),"malformed source rejected");
    std::cout<<"PASS: cross-page crop, exact pixels, background, reverse/clamped drag, alpha, scale, bounds.\n";
}catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}}
