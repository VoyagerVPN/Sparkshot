#include "ui.hpp"
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <array>
#include <vector>
namespace sparkshot::ui {
using Microsoft::WRL::ComPtr;
void DrawIcon(ID2D1RenderTarget* target, ID2D1SolidColorBrush* brush, Icon icon, D2D1_RECT_F bounds) {
    const float sizeScale=std::min(bounds.right-bounds.left,bounds.bottom-bounds.top)/24;
    const float positionX=(bounds.left+bounds.right)/2-12*sizeScale,positionY=(bounds.top+bounds.bottom)/2-12*sizeScale;
    // Неизменная геометрия иконок общая для кадров; размер задаёт только трансформация.
    struct IconCache{ComPtr<ID2D1Factory> factory;ComPtr<ID2D1StrokeStyle> stroke;std::array<std::vector<ComPtr<ID2D1PathGeometry>>,static_cast<size_t>(Icon::Count)> paths;};
    static thread_local IconCache cache;
    ComPtr<ID2D1Factory> factory;target->GetFactory(&factory);
    if(cache.factory.Get()!=factory.Get()){cache={};cache.factory=factory;}
    if(!cache.stroke)factory->CreateStrokeStyle(D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND,D2D1_CAP_STYLE_ROUND,D2D1_CAP_STYLE_ROUND,D2D1_LINE_JOIN_ROUND),nullptr,0,&cache.stroke);
    const auto& stroke=cache.stroke;
    D2D1_MATRIX_3X2_F previous;target->GetTransform(&previous);
    target->SetTransform(D2D1::Matrix3x2F::Scale(sizeScale,sizeScale)*D2D1::Matrix3x2F::Translation(positionX,positionY)*previous);
    constexpr float scale=1,x=0,y=0;size_t pathIndex=0;
    const auto point=[&](float px,float py){return D2D1::Point2F(x+px*scale,y+py*scale);};
    const auto path=[&](std::initializer_list<D2D1_POINT_2F> points,bool fill=false){
        if(points.size()<2)return;auto& paths=cache.paths[static_cast<size_t>(icon)];const size_t index=pathIndex++;if(paths.size()<=index)paths.resize(index+1);auto& geometry=paths[index];
        if(!geometry){ComPtr<ID2D1GeometrySink> sink;
        if(FAILED(factory->CreatePathGeometry(&geometry))||FAILED(geometry->Open(&sink))){geometry.Reset();return;}
        auto i=points.begin();sink->BeginFigure(point(i->x,i->y),fill?D2D1_FIGURE_BEGIN_FILLED:D2D1_FIGURE_BEGIN_HOLLOW);
        for(++i;i!=points.end();++i)sink->AddLine(point(i->x,i->y));
        sink->EndFigure(fill?D2D1_FIGURE_END_CLOSED:D2D1_FIGURE_END_OPEN);if(FAILED(sink->Close())){geometry.Reset();return;}}
        if(fill)target->FillGeometry(geometry.Get(),brush);else target->DrawGeometry(geometry.Get(),brush,1.7f*scale,stroke.Get());
    };
    const auto line=[&](float ax,float ay,float bx,float by){target->DrawLine(point(ax,ay),point(bx,by),brush,1.7f*scale,stroke.Get());};
    const auto rect=[&](float l,float t,float r,float b,float radius=2.0f){target->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x+l*scale,y+t*scale,x+r*scale,y+b*scale),radius*scale,radius*scale),brush,1.7f*scale);};
    switch(icon) {
    case Icon::Select:path({{5,3},{5,19},{10,15},{14,21},{17,19},{13,13},{19,12},{5,3}});break;
    case Icon::Pen:path({{4,20},{5,14},{16,3},{21,8},{10,19},{4,20}});line(14,5,19,10);line(5,14,10,19);break;
    case Icon::Line:line(5,19,19,5);target->FillEllipse(D2D1::Ellipse(point(5,19),1.5f*scale,1.5f*scale),brush);target->FillEllipse(D2D1::Ellipse(point(19,5),1.5f*scale,1.5f*scale),brush);break;
    case Icon::Arrow:line(5,19,16,8);path({{20,4},{18,14},{10,6}},true);break;
    case Icon::Rectangle:rect(4,5,20,19,1);break;
    case Icon::Eraser:path({{3,14},{13,4},{21,12},{13,20},{9,20},{3,14}});line(8,9,16,17);line(14,21,21,21);break;
    case Icon::Text:path({{4,7},{4,4},{20,4},{20,7}});line(12,4,12,20);line(8,20,16,20);break;
    case Icon::Pixelate:for(int row=0;row<3;++row)for(int col=0;col<3;++col){const auto r=D2D1::RectF(x+(4+col*6)*scale,y+(4+row*6)*scale,x+(8+col*6)*scale,y+(8+row*6)*scale);if((row+col)%2)target->FillRectangle(r,brush);else target->DrawRectangle(r,brush,scale);}break;
    case Icon::Number:target->DrawEllipse(D2D1::Ellipse(point(12,12),9*scale,9*scale),brush,1.7f*scale);path({{9,9},{12,7},{12,17}});line(9,17,15,17);break;
    case Icon::Eyedropper:path({{4,20},{5,15},{15,5},{19,9},{9,19},{4,20}});line(12,4,20,12);path({{16,5},{19,2},{22,5},{19,8}});line(6,14,10,18);break;
    case Icon::Ellipse:target->DrawEllipse(D2D1::Ellipse(point(12,12),9*scale,7*scale),brush,1.7f*scale);break;
    case Icon::Fill:path({{11,3},{20,12},{12,20},{3,11},{11,3}});line(5,10,18,10);line(8,2,13,7);path({{21,14},{18,19},{19,22},{22,22},{23,19},{21,14}},true);break;
    case Icon::Copy:rect(8,8,21,21);path({{5,16},{3,16},{3,3},{16,3},{16,5}});break;
    case Icon::Save:path({{4,3},{17,3},{21,7},{21,21},{3,21},{3,3}});rect(7,3,16,10,0);rect(7,15,17,21,0);break;
    case Icon::Folder:path({{3,7},{3,5},{10,5},{12,8},{21,8},{21,20},{3,20},{3,7}});line(3,11,21,11);break;
    case Icon::Close:line(6,6,18,18);line(18,6,6,18);break;
    case Icon::Undo:path({{9,5},{4,10},{9,15}});path({{4,10},{14,10},{19,12},{20,16},{19,20}});break;
    case Icon::Redo:path({{15,5},{20,10},{15,15}});path({{20,10},{10,10},{5,12},{4,16},{5,20}});break;
    case Icon::Up:path({{5,14},{12,7},{19,14}});line(12,7,12,21);break;
    case Icon::Check:path({{4,12},{9,17},{20,6}});break;
    case Icon::Spark:path({{14,2},{5,13},{11,13},{9,22},{20,10},{14,10},{14,2}},true);break;
    case Icon::Exit:path({{11,3},{4,3},{4,21},{11,21}});line(11,12,22,12);path({{17,7},{22,12},{17,17}});break;
    case Icon::Add:line(5,12,19,12);line(12,5,12,19);break;
    case Icon::Trash:line(4,6,20,6);path({{6,6},{7,21},{17,21},{18,6}});path({{9,6},{9,3},{15,3},{15,6}});line(10,10,10,17);line(14,10,14,17);break;
    case Icon::Settings:target->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(12,12),6,6),brush,1.8f);target->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(12,12),2,2),brush,1.8f);for(int i=0;i<8;++i){const float angle=i*3.14159265358979323846f/4;line(12+std::cos(angle)*7,12+std::sin(angle)*7,12+std::cos(angle)*10,12+std::sin(angle)*10);}break;
    case Icon::Keyboard:rect(2,5,22,19,3);for(int row=0;row<2;++row)for(int col=0;col<5;++col)target->FillEllipse(D2D1::Ellipse(point(5+col*3.5f,8+row*3.5f),.8f,.8f),brush);line(7,16,17,16);break;
    case Icon::Startup:target->DrawEllipse(D2D1::Ellipse(point(12,12),9,9),brush,1.7f);path({{10,6},{17,12},{10,18}},true);break;
    case Icon::Info:target->DrawEllipse(D2D1::Ellipse(point(12,12),9,9),brush,1.7f);target->FillEllipse(D2D1::Ellipse(point(12,7),1,1),brush);line(12,11,12,17);break;
    case Icon::ChevronRight:path({{9,5},{16,12},{9,19}});break;
    case Icon::Count:break;
    }
    target->SetTransform(previous);
}
unsigned HsvToRgb(float hue,float saturation,float value){
    hue=std::fmod(std::max(0.0f,hue),360.0f)/60;
    const float c=value*saturation,m=value-c,secondary=c*(1-std::abs(std::fmod(hue,2.0f)-1));
    float r=0,g=0,b=0;
    if(hue<1){r=c;g=secondary;}else if(hue<2){r=secondary;g=c;}else if(hue<3){g=c;b=secondary;}else if(hue<4){g=secondary;b=c;}else if(hue<5){r=secondary;b=c;}else{r=c;b=secondary;}
    const auto byte=[](float v){return static_cast<unsigned>(std::clamp(std::lround(v*255),0L,255L));};
    return(byte(r+m)<<16)|(byte(g+m)<<8)|byte(b+m);
}
void RgbToHsv(unsigned rgb,float& hue,float& saturation,float& value){
    const float r=((rgb>>16)&255)/255.0f,g=((rgb>>8)&255)/255.0f,b=(rgb&255)/255.0f;
    value=std::max({r,g,b});const float low=std::min({r,g,b}),delta=value-low;saturation=value==0?0:delta/value;
    if(delta==0)hue=0;else if(value==r)hue=60*std::fmod((g-b)/delta,6.0f);else if(value==g)hue=60*((b-r)/delta+2);else hue=60*((r-g)/delta+4);
    if(hue<0)hue+=360;
}
std::vector<std::uint32_t> ColorFieldPixels(int width,int height){
    std::vector<std::uint32_t> pixels(static_cast<size_t>(width)*height);
    for(int y=0;y<height;++y)for(int x=0;x<width;++x)pixels[static_cast<size_t>(y)*width+x]=0xff000000|HsvToRgb(x*360.0f/(width-1),1-y/static_cast<float>(height-1),1);
    return pixels;
}
std::vector<std::uint32_t> BrightnessPixels(int width,int height,float hue,float saturation){
    std::vector<std::uint32_t> pixels(static_cast<size_t>(width)*height);
    for(int y=0;y<height;++y){const auto rgb=0xff000000|HsvToRgb(hue,saturation,1-y/static_cast<float>(height-1));std::fill_n(pixels.begin()+static_cast<size_t>(y)*width,width,rgb);}
    return pixels;
}
void ColorFieldPosition(D2D1_RECT_F bounds,D2D1_POINT_2F point,float& hue,float& saturation){
    hue=std::clamp((point.x-bounds.left)/(bounds.right-bounds.left),0.0f,1.0f)*360;
    saturation=1-std::clamp((point.y-bounds.top)/(bounds.bottom-bounds.top),0.0f,1.0f);
}
void DrawColorField(ID2D1RenderTarget* target,ID2D1SolidColorBrush* brush,ID2D1Bitmap* bitmap,D2D1_RECT_F bounds,float hue,float saturation){
    if(!bitmap)return;target->DrawBitmap(bitmap,bounds);
    const auto point=D2D1::Point2F(bounds.left+hue/360*(bounds.right-bounds.left),bounds.top+(1-saturation)*(bounds.bottom-bounds.top));
    brush->SetColor(D2D1::ColorF(0x080d16));target->DrawEllipse(D2D1::Ellipse(point,4,4),brush,4);brush->SetColor(D2D1::ColorF(0xffffff));target->DrawEllipse(D2D1::Ellipse(point,4,4),brush,1.5f);
}
}
