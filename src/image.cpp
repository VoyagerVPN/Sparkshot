#include "image.hpp"
#include <wincodec.h>
#include <wrl/client.h>
#include <stdexcept>
#include <cstring>

namespace sparkshot {
using Microsoft::WRL::ComPtr;
namespace {
void Check(HRESULT result) { if (FAILED(result)) throw std::runtime_error("Ошибка Windows API при обработке изображения"); }
}
Image CaptureDesktop() {
    Image image;
    image.x = GetSystemMetrics(SM_XVIRTUALSCREEN);
    image.y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    image.width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    image.height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    const HDC screen = GetDC(nullptr);
    const HDC memory = CreateCompatibleDC(screen);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = image.width;
    info.bmiHeader.biHeight = -image.height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    void* bits{};
    const HBITMAP bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ previous = bitmap ? SelectObject(memory, bitmap) : nullptr;
    const bool ok = bits && BitBlt(memory, 0, 0, image.width, image.height, screen, image.x, image.y, SRCCOPY | CAPTUREBLT);
    if (ok) {
        const auto* pixels = static_cast<std::uint32_t*>(bits);
        image.pixels.assign(pixels, pixels + static_cast<size_t>(image.width) * image.height);
        for (auto& pixel : image.pixels) pixel |= 0xff000000;
    }
    if (previous) SelectObject(memory, previous);
    if (bitmap) DeleteObject(bitmap);
    DeleteDC(memory);
    ReleaseDC(nullptr, screen);
    if (!ok) throw std::runtime_error("Не удалось захватить рабочий стол");
    return image;
}
Image DemoImage() {
    Image image{0, 0, 1200, 780, {}};
    image.pixels.resize(static_cast<size_t>(image.width) * image.height);
    for (int y = 0; y < image.height; ++y) for (int x = 0; x < image.width; ++x) {
        std::uint32_t color = 0x151c28;
        if (x > 110 && x < 1080 && y > 105 && y < 640) color = 0x222e41;
        if (x > 160 && x < 1030 && y > 185 && y < 195) color = 0x8aa2c1;
        if (x > 160 && x < 570 && y > 240 && y < 570) color = 0x3b63ab;
        if (x > 615 && x < 1020 && y > 240 && y < 390) color = 0x2a4354;
        if (x > 615 && x < 1020 && y > 420 && y < 570) color = 0x58436b;
        image.pixels[static_cast<size_t>(y) * image.width + x] = 0xff000000 | color;
    }
    return image;
}
std::vector<std::uint8_t> EncodePng(const Image& image) {
    ComPtr<IWICImagingFactory> factory;
    Check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)));
    ComPtr<IStream> stream;
    Check(CreateStreamOnHGlobal(nullptr, TRUE, &stream));
    ComPtr<IWICBitmapEncoder> encoder;
    Check(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder));
    Check(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache));
    ComPtr<IWICBitmapFrameEncode> frame;
    Check(encoder->CreateNewFrame(&frame, nullptr));
    Check(frame->Initialize(nullptr));
    Check(frame->SetSize(image.width, image.height));
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    Check(frame->SetPixelFormat(&format));
    if (format != GUID_WICPixelFormat32bppBGRA) throw std::runtime_error("Неожиданный формат PNG");
    Check(frame->WritePixels(image.height, image.width * 4, static_cast<UINT>(image.pixels.size() * 4), reinterpret_cast<BYTE*>(const_cast<std::uint32_t*>(image.pixels.data()))));
    Check(frame->Commit());
    Check(encoder->Commit());
    STATSTG stats{};
    Check(stream->Stat(&stats, STATFLAG_NONAME));
    std::vector<std::uint8_t> result(static_cast<size_t>(stats.cbSize.QuadPart));
    LARGE_INTEGER zero{};
    Check(stream->Seek(zero, STREAM_SEEK_SET, nullptr));
    ULONG read{};
    Check(stream->Read(result.data(), static_cast<ULONG>(result.size()), &read));
    if (read != result.size()) throw std::runtime_error("Не удалось прочитать PNG");
    return result;
}
void CopyImage(const Image& image, HWND owner) {
    // Копирование размещает снимок тремя форматами: CF_DIB и CF_DIBV5 с нижним
    // порядком строк (положительная высота), как GDI-приложения вроде Lightshot
    // и системный Print Screen, плюс зарегистрированный формат "PNG" для
    // приложений, которые читают только его (мессенджеры, Electron).
    // Отрицательная высота DIB корректно читалась Paint и GDI, но не всеми
    // читателями изображений из буфера обмена.
    const UINT pngFormat = RegisterClipboardFormatW(L"PNG");
    const auto png = EncodePng(image);
    const size_t pixelBytes = image.pixels.size() * 4;
    const HGLOBAL pngBlock = pngFormat ? GlobalAlloc(GMEM_MOVEABLE, png.size()) : nullptr;
    const HGLOBAL v5Block = GlobalAlloc(GMEM_MOVEABLE, sizeof(BITMAPV5HEADER) + pixelBytes);
    const HGLOBAL dibBlock = GlobalAlloc(GMEM_MOVEABLE, sizeof(BITMAPINFOHEADER) + pixelBytes);
    bool allocated = (!pngFormat || pngBlock) && v5Block && dibBlock;
    if (allocated) {
        if (pngBlock) {
            auto* data = static_cast<std::uint8_t*>(GlobalLock(pngBlock));
            if (data) { std::memcpy(data, png.data(), png.size()); GlobalUnlock(pngBlock); } else allocated = false;
        }
        // Первые 40 байт BITMAPV5HEADER совместимы по раскладке с BITMAPINFOHEADER.
        BITMAPV5HEADER header{};
        header.bV5Size = sizeof(BITMAPV5HEADER);
        header.bV5Width = image.width;
        header.bV5Height = image.height;
        header.bV5Planes = 1;
        header.bV5BitCount = 32;
        header.bV5Compression = BI_RGB;
        header.bV5SizeImage = static_cast<DWORD>(pixelBytes);
        for (int pass = 0; allocated && pass < 2; ++pass) {
            const HGLOBAL block = pass ? dibBlock : v5Block;
            auto* target = static_cast<std::uint8_t*>(GlobalLock(block));
            if (!target) { allocated = false; break; }
            std::memcpy(target, &header, pass ? sizeof(BITMAPINFOHEADER) : sizeof(BITMAPV5HEADER));
            auto* pixels = reinterpret_cast<std::uint32_t*>(target + (pass ? sizeof(BITMAPINFOHEADER) : sizeof(BITMAPV5HEADER)));
            for (int y = 0; y < image.height; ++y)
                std::memcpy(pixels + static_cast<size_t>(image.height - 1 - y) * image.width, image.pixels.data() + static_cast<size_t>(y) * image.width, static_cast<size_t>(image.width) * 4);
            GlobalUnlock(block);
        }
    }
    if (!allocated) {
        if (pngBlock) GlobalFree(pngBlock);
        if (v5Block) GlobalFree(v5Block);
        if (dibBlock) GlobalFree(dibBlock);
        throw std::runtime_error("Недостаточно памяти для буфера обмена");
    }
    if (!OpenClipboard(owner)) {
        GlobalFree(pngBlock); GlobalFree(v5Block); GlobalFree(dibBlock);
        throw std::runtime_error("Буфер обмена занят другим приложением");
    }
    EmptyClipboard();
    // Система забирает память успешно размещённых блоков; неудачные освобождаем сами.
    const bool pngOk = !pngFormat || SetClipboardData(pngFormat, pngBlock) != nullptr;
    const bool v5Ok = SetClipboardData(CF_DIBV5, v5Block) != nullptr;
    const bool dibOk = SetClipboardData(CF_DIB, dibBlock) != nullptr;
    CloseClipboard();
    if (!pngOk) GlobalFree(pngBlock);
    if (!v5Ok) GlobalFree(v5Block);
    if (!dibOk) GlobalFree(dibBlock);
    if (!dibOk && !v5Ok && !pngOk) throw std::runtime_error("Не удалось скопировать изображение");
}
}
