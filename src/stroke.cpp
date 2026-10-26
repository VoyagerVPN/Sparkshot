#include "stroke.hpp"
#include <algorithm>
#include <cmath>
namespace sparkshot {
namespace {
bool Empty(D2D1_RECT_U r){return r.left>=r.right||r.top>=r.bottom;}
D2D1_RECT_U Union(D2D1_RECT_U a,D2D1_RECT_U b){
    if(Empty(a))return b;if(Empty(b))return a;
    return D2D1::RectU(std::min(a.left,b.left),std::min(a.top,b.top),std::max(a.right,b.right),std::max(a.bottom,b.bottom));
}
}
void StrokeRaster::Begin(int width,int height,D2D1_POINT_2F origin,float radius){
    Reset();width_=width;height_=height;origin_=last_=origin;radius_=radius;
    pixels_.assign(static_cast<size_t>(width)*height,0);Segment(origin,origin,radius,radius);
}
void StrokeRaster::Add(D2D1_POINT_2F point,float radius){
    if(pixels_.empty())return;
    const float dx=point.x-last_.x,dy=point.y-last_.y;
    // Короткие участки ограничивают площадь обхода даже при резком движении по диагонали.
    const int count=std::max(1,static_cast<int>(std::ceil(std::hypot(dx,dy)/64)));
    auto previous=last_;float previousRadius=radius_;
    for(int i=1;i<=count;++i){const float t=static_cast<float>(i)/count;
        const auto next=D2D1::Point2F(last_.x+dx*t,last_.y+dy*t);const float nextRadius=radius_+(radius-radius_)*t;
        Segment(previous,next,previousRadius,nextRadius);previous=next;previousRadius=nextRadius;
    }
    last_=point;radius_=radius;
}
void StrokeRaster::Segment(D2D1_POINT_2F from,D2D1_POINT_2F to,float fromRadius,float toRadius){
    const float dx=to.x-from.x,dy=to.y-from.y,length=std::hypot(dx,dy);
    if(length<.001f||std::abs(toRadius-fromRadius)>=length){
        // При неподвижном колесе объединение кругов равно самому большому кругу.
        if(toRadius>fromRadius)from=to;fromRadius=std::max(fromRadius,toRadius);to=from;toRadius=fromRadius;
    }
    const float radius=std::max(fromRadius,toRadius)+1;
    const auto rect=D2D1::RectU(
        static_cast<UINT32>(std::clamp(static_cast<int>(std::floor(std::min(from.x,to.x)-radius)),0,width_)),
        static_cast<UINT32>(std::clamp(static_cast<int>(std::floor(std::min(from.y,to.y)-radius)),0,height_)),
        static_cast<UINT32>(std::clamp(static_cast<int>(std::ceil(std::max(from.x,to.x)+radius)),0,width_)),
        static_cast<UINT32>(std::clamp(static_cast<int>(std::ceil(std::max(from.y,to.y)+radius)),0,height_)));
    if(Empty(rect))return;
    const float segmentLength=std::hypot(to.x-from.x,to.y-from.y);
    const float ux=segmentLength>.001f?(to.x-from.x)/segmentLength:0,uy=segmentLength>.001f?(to.y-from.y)/segmentLength:0;
    const float slope=segmentLength>.001f?(toRadius-fromRadius)/segmentLength:0;
    const float correction=slope/std::sqrt(std::max(.000001f,1-slope*slope));
    for(UINT32 y=rect.top;y<rect.bottom;++y)for(UINT32 x=rect.left;x<rect.right;++x){
        auto& pixel=pixels_[static_cast<size_t>(y)*width_+x];if(pixel==0xffffffff)continue;
        const float px=x+.5f-from.x,py=y+.5f-from.y;
        const float along=px*ux+py*uy,across=std::abs(px*uy-py*ux);
        // Ближайшая точка огибающей кругов с линейно меняющимся радиусом.
        const float t=std::clamp(along+across*correction,0.0f,segmentLength);
        const float distance=segmentLength>.001f?std::sqrt((along-t)*(along-t)+across*across):std::sqrt(px*px+py*py);
        const auto alpha=static_cast<unsigned>(std::clamp(fromRadius+t*slope+.5f-distance,0.0f,1.0f)*255+.5f);
        // Максимум покрытия сохраняет равномерную прозрачность маркера на пересечениях.
        if(alpha>(pixel>>24))pixel=alpha*0x01010101u;
    }
    bounds_=Union(bounds_,rect);dirty_=Union(dirty_,rect);
}
std::shared_ptr<const StrokeData> StrokeRaster::Finish()const{
    if(Empty(bounds_))return {};
    auto result=std::make_shared<StrokeData>();result->width=static_cast<int>(bounds_.right-bounds_.left);result->height=static_cast<int>(bounds_.bottom-bounds_.top);
    result->offsetX=bounds_.left-origin_.x;result->offsetY=bounds_.top-origin_.y;
    result->pixels.resize(static_cast<size_t>(result->width)*result->height);
    for(int y=0;y<result->height;++y)std::copy_n(pixels_.data()+static_cast<size_t>(bounds_.top+y)*width_+bounds_.left,result->width,result->pixels.data()+static_cast<size_t>(y)*result->width);
    return result;
}
void StrokeRaster::Reset(){width_=height_=0;bounds_=dirty_={};std::vector<std::uint32_t>().swap(pixels_);}
}
