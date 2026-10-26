#include "renderer.hpp"
#include <stdexcept>
#include <cstring>

namespace sparkshot {
using Microsoft::WRL::ComPtr;
namespace {
void Check(HRESULT result){if(FAILED(result))throw std::runtime_error("Не удалось подготовить аппаратную отрисовку Sparkshot");}
}
Renderer::~Renderer(){Reset();}
HRESULT Renderer::CreateDevice(ID2D1Factory1* factory,D3D_DRIVER_TYPE driver){
    device_.Reset();immediate_.Reset();d2dDevice_.Reset();canvas_.Reset();
    HRESULT result=D3D11CreateDevice(nullptr,driver,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,nullptr,0,D3D11_SDK_VERSION,&device_,nullptr,&immediate_);
    ComPtr<IDXGIDevice> dxgi;if(SUCCEEDED(result))result=device_.As(&dxgi);
    if(SUCCEEDED(result))result=factory->CreateDevice(dxgi.Get(),&d2dDevice_);
    if(SUCCEEDED(result))result=d2dDevice_->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE,&canvas_);
    return result;
}
void Renderer::Initialize(ID2D1Factory1* factory,bool software){
    if(device_&&canvas_)return;
    // WARP — резерв при недоступном аппаратном устройстве, а не реакция на временную загрузку игры.
    hardware_=!software&&SUCCEEDED(CreateDevice(factory,D3D_DRIVER_TYPE_HARDWARE));
    if(!hardware_)Check(CreateDevice(factory,D3D_DRIVER_TYPE_WARP));
}
void Renderer::Create(ID2D1Factory1* factory,HWND window,UINT width,UINT height,bool offscreen,bool software){
    ReleaseFrame();Initialize(factory,software);width_=width;height_=height;
    if(!offscreen){
        ComPtr<IDXGIDevice> dxgi;ComPtr<IDXGIAdapter> adapter;ComPtr<IDXGIFactory2> dxgiFactory;
        Check(device_.As(&dxgi));Check(dxgi->GetAdapter(&adapter));Check(adapter->GetParent(IID_PPV_ARGS(&dxgiFactory)));
        DXGI_SWAP_CHAIN_DESC1 desc{};desc.Width=width;desc.Height=height;desc.Format=DXGI_FORMAT_B8G8R8A8_UNORM;desc.SampleDesc.Count=1;
        desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;desc.BufferCount=2;desc.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;desc.Scaling=DXGI_SCALING_STRETCH;
        desc.AlphaMode=DXGI_ALPHA_MODE_IGNORE;desc.Flags=DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
        ComPtr<IDXGISwapChain1> swap;Check(dxgiFactory->CreateSwapChainForHwnd(device_.Get(),window,&desc,nullptr,nullptr,&swap));Check(swap.As(&swap_));
        Check(dxgiFactory->MakeWindowAssociation(window,DXGI_MWA_NO_ALT_ENTER));Check(swap_->SetMaximumFrameLatency(1));
        ready_=swap_->GetFrameLatencyWaitableObject();if(!ready_)throw std::runtime_error("Нет события готовности кадра DXGI");
        ComPtr<IDXGISurface> surface;Check(swap_->GetBuffer(0,IID_PPV_ARGS(&surface)));
        const auto props=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET|D2D1_BITMAP_OPTIONS_CANNOT_DRAW,D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_IGNORE),96,96);
        Check(canvas_->CreateBitmapFromDxgiSurface(surface.Get(),&props,&backBuffer_));
    }else{
        // Изолированные проверки не создают видимого окна и не вмешиваются в ввод пользователя.
        ready_=CreateEventW(nullptr,FALSE,TRUE,nullptr);if(!ready_)throw std::runtime_error("Нет события для проверки отрисовки");
    }
    D3D11_TEXTURE2D_DESC desc{};desc.Width=width;desc.Height=height;desc.MipLevels=1;desc.ArraySize=1;desc.Format=DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count=1;desc.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;Check(device_->CreateTexture2D(&desc,nullptr,&frameTexture_));
    ComPtr<IDXGISurface> surface;Check(frameTexture_.As(&surface));
    const auto props=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET,D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_IGNORE),96,96);
    Check(canvas_->CreateBitmapFromDxgiSurface(surface.Get(),&props,&frame_));canvas_->SetDpi(96,96);canvas_->SetTarget(frame_.Get());
}
void Renderer::ReleaseFrame(){
    if(canvas_)canvas_->SetTarget(nullptr);
    backBuffer_.Reset();frame_.Reset();frameTexture_.Reset();swap_.Reset();
    // После освобождения swap chain сбрасываем отложенные ссылки D3D; устройство сохраняем для следующего захвата.
    if(immediate_){immediate_->ClearState();immediate_->Flush();}
    if(ready_){CloseHandle(ready_);ready_=nullptr;}width_=height_=0;
}
void Renderer::Reset(){
    ReleaseFrame();canvas_.Reset();d2dDevice_.Reset();immediate_.Reset();device_.Reset();hardware_=false;
}
ID2D1RenderTarget* Renderer::Target()const{return canvas_.Get();}
HANDLE Renderer::FrameReady()const{return ready_;}
bool Renderer::Hardware()const{return hardware_;}
HRESULT Renderer::Present(){
    if(!swap_){SetEvent(ready_);return S_OK;}
    // Единственный полный проход — копирование готового кадра; сложный UI остаётся в кэше.
    canvas_->SetTarget(backBuffer_.Get());canvas_->BeginDraw();canvas_->SetTransform(D2D1::Matrix3x2F::Identity());
    canvas_->DrawBitmap(frame_.Get());HRESULT result=canvas_->EndDraw();canvas_->SetTarget(frame_.Get());
    if(SUCCEEDED(result))result=swap_->Present(1,DXGI_PRESENT_DO_NOT_WAIT);
    return result;
}
std::vector<std::uint32_t> Renderer::ReadPixels(){
    D3D11_TEXTURE2D_DESC desc{};frameTexture_->GetDesc(&desc);desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;Check(device_->CreateTexture2D(&desc,nullptr,&staging));immediate_->CopyResource(staging.Get(),frameTexture_.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};Check(immediate_->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped));
    std::vector<std::uint32_t> result(static_cast<size_t>(width_)*height_);
    for(UINT y=0;y<height_;++y)std::memcpy(result.data()+static_cast<size_t>(y)*width_,static_cast<const char*>(mapped.pData)+static_cast<size_t>(y)*mapped.RowPitch,width_*4);
    immediate_->Unmap(staging.Get(),0);return result;
}
}
