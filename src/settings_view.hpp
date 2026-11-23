#pragma once
#include <windows.h>
#include <d2d1.h>
#include <wrl/client.h>

namespace sparkshot {
class SettingsView {
public:
    ~SettingsView();
    void SetFonts(HWND dialog);
    void Paint(HWND dialog,HDC dc,ID2D1Factory* factory,bool hasStatus);
    HBRUSH StaticColor(HWND control,HDC dc)const;
    void DrawButton(const DRAWITEMSTRUCT& item,bool selected)const;
private:
    HFONT bodyFont_{},titleFont_{},sectionFont_{};
    HBRUSH background_{CreateSolidBrush(RGB(28,28,28))},card_{CreateSolidBrush(RGB(38,38,38))};
    Microsoft::WRL::ComPtr<ID2D1DCRenderTarget> canvas_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush_;
};
}
