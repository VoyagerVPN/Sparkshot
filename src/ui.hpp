#pragma once
#include <d2d1.h>
#include <vector>
#include <cstdint>
namespace sparkshot::ui {
enum class Icon { Select, Pen, Line, Arrow, Rectangle, Eraser, Text, Pixelate, Number, Eyedropper, Ellipse, Fill, Copy, Save, Folder, Close, Undo, Redo, Up, Check, Spark, Exit, Add, Trash, Settings, Keyboard, Startup, Info, ChevronRight, Count };
void DrawIcon(ID2D1RenderTarget* target, ID2D1SolidColorBrush* brush, Icon icon, D2D1_RECT_F bounds);
unsigned HsvToRgb(float hue, float saturation, float value);
void RgbToHsv(unsigned rgb, float& hue, float& saturation, float& value);
std::vector<std::uint32_t> ColorFieldPixels(int width,int height);
std::vector<std::uint32_t> BrightnessPixels(int width,int height,float hue,float saturation);
void ColorFieldPosition(D2D1_RECT_F bounds,D2D1_POINT_2F point,float& hue,float& saturation);
void DrawColorField(ID2D1RenderTarget* target,ID2D1SolidColorBrush* brush,ID2D1Bitmap* bitmap,D2D1_RECT_F bounds,float hue,float saturation);
}
