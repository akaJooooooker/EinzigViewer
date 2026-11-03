#include "ImageComposition.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace image_composition {
std::shared_ptr<ImageData> Compose(const std::vector<Layer>& layers,RECT selection,RECT viewport,std::wstring& error) {
    error.clear();
    if(selection.left>selection.right)std::swap(selection.left,selection.right);
    if(selection.top>selection.bottom)std::swap(selection.top,selection.bottom);
    selection.left=std::max(selection.left,viewport.left);selection.right=std::min(selection.right,viewport.right);
    selection.top=std::max(selection.top,viewport.top);selection.bottom=std::min(selection.bottom,viewport.bottom);
    const auto width=static_cast<std::int64_t>(selection.right)-selection.left;
    const auto height=static_cast<std::int64_t>(selection.bottom)-selection.top;
    if(width<=0 || height<=0 || width>65536 || height>65536 || width*height>64LL*1024*1024 || layers.size()>128) {
        error=L"選取範圍為空或過大。 / The selected region is empty or too large.";return {};
    }
    for(const auto& layer:layers) {
        const auto& p=layer.bounds;const auto& image=layer.image;
        if(!image || !image->width || !image->height || static_cast<std::uint64_t>(image->width)*4>image->stride ||
            static_cast<std::uint64_t>(image->stride)*image->height>image->pixels.size() ||
            !std::isfinite(p.left) || !std::isfinite(p.top) || !std::isfinite(p.right) || !std::isfinite(p.bottom) ||
            !std::isfinite(p.right-p.left) || !std::isfinite(p.bottom-p.top) || p.right<=p.left || p.bottom<=p.top) {
            error=L"選取圖片的像素或位置無效。 / Invalid image pixels or position.";return {};
        }
    }
    try {
        auto output=std::make_shared<ImageData>();output->width=static_cast<unsigned>(width);output->height=static_cast<unsigned>(height);
        output->stride=output->width*4;output->pixels.resize(static_cast<std::size_t>(output->stride)*output->height);
        for(std::size_t i=0;i<output->pixels.size();i+=4) {
            output->pixels[i]=std::byte{0x12};output->pixels[i+1]=std::byte{0x0e};
            output->pixels[i+2]=std::byte{0x0b};output->pixels[i+3]=std::byte{255};
        }
        for(const auto& layer:layers) {
            const auto& p=layer.bounds;const auto& image=*layer.image;
            const auto left=static_cast<LONG>(std::ceil(std::clamp(p.left-0.5,static_cast<double>(selection.left),static_cast<double>(selection.right))));
            const auto right=static_cast<LONG>(std::ceil(std::clamp(p.right-0.5,static_cast<double>(selection.left),static_cast<double>(selection.right))));
            const auto top=static_cast<LONG>(std::ceil(std::clamp(p.top-0.5,static_cast<double>(selection.top),static_cast<double>(selection.bottom))));
            const auto bottom=static_cast<LONG>(std::ceil(std::clamp(p.bottom-0.5,static_cast<double>(selection.top),static_cast<double>(selection.bottom))));
            for(LONG y=top;y<bottom;++y)for(LONG x=left;x<right;++x) {
                const double sx=std::clamp((x+0.5-p.left)*image.width/(p.right-p.left)-0.5,0.0,static_cast<double>(image.width-1));
                const double sy=std::clamp((y+0.5-p.top)*image.height/(p.bottom-p.top)-0.5,0.0,static_cast<double>(image.height-1));
                const auto x0=static_cast<unsigned>(sx),y0=static_cast<unsigned>(sy);
                const auto x1=std::min(x0+1,image.width-1),y1=std::min(y0+1,image.height-1);
                const double fx=sx-x0,fy=sy-y0;
                double channels[4]{};
                for(unsigned c=0;c<4;++c) {
                    const auto value=[&](unsigned px,unsigned py){return std::to_integer<unsigned>(image.pixels[static_cast<std::size_t>(py)*image.stride+px*4+c]);};
                    channels[c]=(value(x0,y0)*(1-fx)+value(x1,y0)*fx)*(1-fy)+(value(x0,y1)*(1-fx)+value(x1,y1)*fx)*fy;
                }
                const auto offset=static_cast<std::size_t>(y-selection.top)*output->stride+static_cast<std::size_t>(x-selection.left)*4;
                for(unsigned c=0;c<3;++c) {
                    const double background=std::to_integer<unsigned>(output->pixels[offset+c]);
                    output->pixels[offset+c]=static_cast<std::byte>(std::clamp(std::lround(channels[c]+background*(1-channels[3]/255)),0L,255L));
                }
            }
        }
        return output;
    }catch(...){error=L"無法建立選取圖片，可能記憶體不足。 / Could not create the selected image; memory may be insufficient.";return {};}
}
}
