#pragma once
#include "drawing.hpp"
#include "image.hpp"
namespace sparkshot {
Annotation FillConnectedRegion(const Image& source,int x,int y,unsigned color);
}
