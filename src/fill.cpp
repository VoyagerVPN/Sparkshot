#include "fill.hpp"
#include <algorithm>
#include <cmath>
namespace sparkshot {
Annotation FillConnectedRegion(const Image& source,int x,int y,unsigned color){
    Annotation result;result.tool=Tool::Fill;result.color=color;
    if(x<0||y<0||x>=source.width||y>=source.height||source.pixels.empty())return result;
    const auto seed=source.pixels[static_cast<size_t>(y)*source.width+x];
    if((seed&0xffffff)==color)return result;
    const auto matches=[&](int px,int py){const auto pixel=source.pixels[static_cast<size_t>(py)*source.width+px];for(int shift:{0,8,16})if(std::abs(static_cast<int>((pixel>>shift)&255)-static_cast<int>((seed>>shift)&255))>16)return false;return true;};
    struct Point{int x,y;};struct Run{int left,right,y;};
    std::vector<Point> pending{{x,y}};std::vector<Run> runs;
    std::vector<std::uint8_t> visited(source.pixels.size());
    int minX=source.width,minY=source.height,maxX=0,maxY=0;
    // Заливка обходит горизонтальные отрезки, не создавая очередь из каждого пикселя.
    while(!pending.empty()){
        const auto point=pending.back();pending.pop_back();
        if(visited[static_cast<size_t>(point.y)*source.width+point.x]||!matches(point.x,point.y))continue;
        int left=point.x,right=point.x;
        while(left>0&&!visited[static_cast<size_t>(point.y)*source.width+left-1]&&matches(left-1,point.y))--left;
        while(right+1<source.width&&!visited[static_cast<size_t>(point.y)*source.width+right+1]&&matches(right+1,point.y))++right;
        for(int px=left;px<=right;++px)visited[static_cast<size_t>(point.y)*source.width+px]=1;
        runs.push_back({left,right,point.y});minX=std::min(minX,left);maxX=std::max(maxX,right);minY=std::min(minY,point.y);maxY=std::max(maxY,point.y);
        for(int nextY:{point.y-1,point.y+1}){if(nextY<0||nextY>=source.height)continue;bool span=false;for(int px=left;px<=right;++px){const bool ok=!visited[static_cast<size_t>(nextY)*source.width+px]&&matches(px,nextY);if(ok&&!span)pending.push_back({px,nextY});span=ok;}}
    }
    if(runs.empty())return result;
    auto fill=std::make_shared<FillData>();fill->width=maxX-minX+1;fill->height=maxY-minY+1;fill->pixels.resize(static_cast<size_t>(fill->width)*fill->height);
    for(const auto run:runs)for(int px=run.left;px<=run.right;++px)fill->pixels[static_cast<size_t>(run.y-minY)*fill->width+px-minX]=0xff000000|color;
    result.points={D2D1::Point2F(static_cast<float>(source.x+minX),static_cast<float>(source.y+minY))};result.fill=std::move(fill);return result;
}
}
