#include "app.hpp"
#include <objbase.h>
#include <commctrl.h>
extern "C" __declspec(dllexport) int RunIsolatedEditor(const wchar_t* options){
    if(FAILED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)))return 1;
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES};InitCommonControlsEx(&controls);
    HMODULE instance{};GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&RunIsolatedEditor),&instance);
    int result{};
    {sparkshot::App app(instance,std::wstring(L"--demo --headless --autostart ")+(options?options:L""));result=app.Run();}
    CoUninitialize();return result;
}
