#pragma once
#include <windows.h>
#include <cstdint>
#include <vector>

namespace sparkshot {
struct Image {
    int x{}, y{}, width{}, height{};
    std::vector<std::uint32_t> pixels;
};
Image CaptureDesktop();
Image DemoImage();
std::vector<std::uint8_t> EncodePng(const Image& image);
void CopyImage(const Image& image, HWND owner);
}
