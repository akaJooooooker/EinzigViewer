#include "ImageRotation.h"
#include <iostream>
#include <stdexcept>

void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(){try{
    ImageData original;original.path=L"original.png";original.width=3;original.height=2;original.stride=16;
    original.sourceWasJpegXr=true;original.hdrToneMappedToSdr=true;
    original.pixels.resize(32,std::byte{0});
    for(unsigned y=0;y<2;++y)for(unsigned x=0;x<3;++x){
        original.pixels[y*16+x*4]=static_cast<std::byte>(y*3+x+1);
        original.pixels[y*16+x*4+3]=std::byte{255};
    }
    std::wstring error;
    for(const auto& item: {std::pair{1,std::vector<int>{4,1,5,2,6,3}},
        std::pair{-1,std::vector<int>{3,6,2,5,1,4}},std::pair{2,std::vector<int>{6,5,4,3,2,1}}}) {
        const auto rotated=RotateImage(original,item.first,error);
        Require(rotated!=nullptr,"rotation succeeds");
        Require(rotated->width==(item.first==2?3U:2U) && rotated->height==(item.first==2?2U:3U),"swapped dimensions match quarter turns");
        for(std::size_t i=0;i<6;++i)Require(static_cast<int>(rotated->pixels[i*4])==item.second[i],"clockwise/counterclockwise pixel placement");
        Require(rotated->path==original.path && rotated->sourceWasJpegXr && rotated->hdrToneMappedToSdr,"metadata preserved");
    }
    auto clockwise=RotateImage(original,1,error);
    auto restored=RotateImage(*clockwise,-1,error);
    Require(restored && restored->width==3 && restored->height==2,"inverse rotation restores dimensions");
    for(unsigned y=0;y<2;++y)for(unsigned x=0;x<3;++x)
        Require(restored->pixels[y*restored->stride+x*4]==original.pixels[y*16+x*4],"inverse restores pixels");
    Require(original.width==3 && original.pixels[0]==std::byte{1},"source is never modified");
    ImageData malformed=original;malformed.pixels.clear();
    Require(!RotateImage(malformed,1,error),"invalid buffer is rejected");
    std::cout<<"PASS: left/right/half turns, padded rows, exact pixels, metadata, original preservation, malformed buffers.\n";
}catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}}
