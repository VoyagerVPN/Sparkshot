#pragma once
#include <windows.h>
#include <d2d1_1.h>
#include <d3d11.h>
#include <dxgi1_3.h>
#include <wrl/client.h>
#include <vector>
#include <cstdint>

namespace sparkshot {
// Рисование и вывод разделены: UI не ждёт занятую очередь GPU внутри обработки ввода.
class Renderer {
public:
    ~Renderer();
    void Initialize(ID2D1Factory1* factory,bool software=false);
    void Create(ID2D1Factory1* factory,HWND window,UINT width,UINT height,bool offscreen=false,bool software=false);
    void ReleaseFrame();
    void Reset();
    ID2D1RenderTarget* Target()const;
    HANDLE FrameReady()const;
    HRESULT Present();
    bool Hardware()const;
    std::vector<std::uint32_t> ReadPixels();
private:
    HRESULT CreateDevice(ID2D1Factory1* factory,D3D_DRIVER_TYPE driver);
    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> immediate_;
    Microsoft::WRL::ComPtr<ID2D1Device> d2dDevice_;
    Microsoft::WRL::ComPtr<ID2D1DeviceContext> canvas_;
    Microsoft::WRL::ComPtr<IDXGISwapChain2> swap_;
    Microsoft::WRL::ComPtr<ID2D1Bitmap1> backBuffer_,frame_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> frameTexture_;
    HANDLE ready_{};
    UINT width_{},height_{};
    bool hardware_{};
};
}
