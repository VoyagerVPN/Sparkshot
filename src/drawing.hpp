#pragma once
#include <d2d1.h>
#include <string>
#include <vector>
#include <memory>
#include <cstdint>
namespace sparkshot {
enum class Tool { Select, Pen, Line, Arrow, Rectangle, Eraser, Text, Pixelate, Number, Eyedropper, Ellipse, Fill };
struct FillData {
    int width{},height{};
    std::vector<std::uint32_t> pixels;
};
struct StrokeData {
    int width{},height{};
    float offsetX{},offsetY{};
    std::vector<std::uint32_t> pixels;
};
struct Annotation {
    Tool tool{Tool::Pen};
    std::vector<D2D1_POINT_2F> points;
    unsigned color{0xff5757};
    float width{3};
    std::wstring text;
    std::vector<float> widths;
    int pixelSize{12};
    std::shared_ptr<const FillData> fill;
    std::shared_ptr<const StrokeData> stroke;
    float fontSize{24};
};
}
