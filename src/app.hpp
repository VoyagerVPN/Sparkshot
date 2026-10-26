#pragma once
#include <windows.h>
#include <memory>
#include <string>

namespace sparkshot {
class App {
public:
    App(HINSTANCE instance, std::wstring command);
    ~App();
    int Run();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
