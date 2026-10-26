#pragma once
#include "drawing.hpp"
namespace sparkshot {
// Маска текущего штриха: старые участки не перестраиваются при движении мыши.
class StrokeRaster {
public:
    void Begin(int width,int height,D2D1_POINT_2F origin,float radius);
    void Add(D2D1_POINT_2F point,float radius);
    std::shared_ptr<const StrokeData> Finish()const;
    void Reset();
    const std::vector<std::uint32_t>& Pixels()const{return pixels_;}
    D2D1_RECT_U Bounds()const{return bounds_;}
    D2D1_RECT_U Dirty()const{return dirty_;}
    void Uploaded(){dirty_={};}
private:
    void Segment(D2D1_POINT_2F from,D2D1_POINT_2F to,float fromRadius,float toRadius);
    int width_{},height_{};
    D2D1_POINT_2F origin_{},last_{};
    float radius_{};
    D2D1_RECT_U bounds_{},dirty_{};
    std::vector<std::uint32_t> pixels_;
};
}
