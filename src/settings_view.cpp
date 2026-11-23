#include "settings_view.hpp"
#include "settings_ids.h"
#include "ui.hpp"
#include <algorithm>
#include <string>

namespace sparkshot {
namespace {
constexpr COLORREF Ink=RGB(235,235,232),Muted=RGB(172,172,168),Accent=RGB(255,200,87);
bool OnBackground(int id){return id==IDC_TITLE||id==IDC_SUBTITLE||id==IDC_STATUS||id==IDC_FOLDER_PATH||id==IDC_EXE_PATH;}
constexpr int SectionTitles[]{IDC_CAPTURE_TITLE,IDC_SAVE_TITLE,IDC_STARTUP_TITLE};
}
SettingsView::~SettingsView(){
    DeleteObject(bodyFont_);DeleteObject(titleFont_);DeleteObject(sectionFont_);DeleteObject(background_);DeleteObject(card_);
}
void SettingsView::SetFonts(HWND dialog){
    LOGFONTW font{};
    const UINT dpi=GetDpiForWindow(dialog);
    wcscpy_s(font.lfFaceName,L"Segoe UI");font.lfQuality=CLEARTYPE_QUALITY;
    font.lfHeight=-MulDiv(14,dpi,96);
    if(HFONT body=CreateFontIndirectW(&font)){
        for(HWND child=GetWindow(dialog,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT))
            SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(body),TRUE);
        DeleteObject(bodyFont_);bodyFont_=body;
    }
    font.lfHeight=-MulDiv(26,dpi,96);font.lfWeight=FW_SEMIBOLD;
    HFONT title=CreateFontIndirectW(&font);
    font.lfHeight=-MulDiv(17,dpi,96);
    HFONT section=CreateFontIndirectW(&font);
    if(title){SendDlgItemMessageW(dialog,IDC_TITLE,WM_SETFONT,reinterpret_cast<WPARAM>(title),TRUE);DeleteObject(titleFont_);titleFont_=title;}
    if(section){
        for(int id:SectionTitles)SendDlgItemMessageW(dialog,id,WM_SETFONT,reinterpret_cast<WPARAM>(section),TRUE);
        DeleteObject(sectionFont_);sectionFont_=section;
    }
}
HBRUSH SettingsView::StaticColor(HWND control,HDC dc)const{
    int id=GetDlgCtrlID(control);
    const bool heading=id==IDC_TITLE||id==IDC_CAPTURE_TITLE||id==IDC_SAVE_TITLE||id==IDC_STARTUP_TITLE;
    SetTextColor(dc,id==IDC_STATUS?Accent:heading||id==IDC_FOLDER_PATH||id==IDC_EXE_PATH?Ink:Muted);
    SetBkMode(dc,TRANSPARENT);
    return OnBackground(id)?background_:card_;
}
void SettingsView::Paint(HWND dialog,HDC dc,ID2D1Factory* factory,bool hasStatus){
    RECT bounds{};GetClientRect(dialog,&bounds);
    if(!canvas_){
        auto props=D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_IGNORE),96,96);
        if(FAILED(factory->CreateDCRenderTarget(&props,&canvas_)))return;
    }
    if(FAILED(canvas_->BindDC(dc,&bounds)))return;
    if(!brush_&&FAILED(canvas_->CreateSolidColorBrush(D2D1::ColorF(0xffffff),&brush_)))return;
    canvas_->BeginDraw();canvas_->Clear(D2D1::ColorF(0x1c1c1c));
    canvas_->SetTransform(D2D1::Matrix3x2F::Scale(bounds.right/680.f,bounds.bottom/664.f));
    auto box=[&](float l,float t,float r,float b,float radius,unsigned color){
        brush_->SetColor(D2D1::ColorF(color));
        canvas_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(l,t,r,b),radius,radius),brush_.Get());
    };
    auto tile=[&](float x,float y,ui::Icon icon){
        box(x,y,x+36,y+36,9,0x333333);brush_->SetColor(D2D1::ColorF(0xdcdcd5));
        ui::DrawIcon(canvas_.Get(),brush_.Get(),icon,D2D1::RectF(x+7,y+7,x+29,y+29));
    };
    box(28,24,76,72,12,0x262626);brush_->SetColor(D2D1::ColorF(0xdcdcd5));
    ui::DrawIcon(canvas_.Get(),brush_.Get(),ui::Icon::Settings,D2D1::RectF(39,35,65,61));
    box(28,94,652,95,0,0x3b3b3b);
    auto card=[&](float top,float bottom,ui::Icon icon){
        box(28,top,652,bottom,12,0x3b3b3b);box(29,top+1,651,bottom-1,11,0x262626);tile(48,top+20,icon);
    };
    card(108,242,ui::Icon::Keyboard);card(258,396,ui::Icon::Folder);card(412,600,ui::Icon::Startup);
    box(48,338,632,378,6,0x1c1c1c);box(48,518,632,554,6,0x1c1c1c);
    if(hasStatus){brush_->SetColor(D2D1::ColorF(0xffc857));ui::DrawIcon(canvas_.Get(),brush_.Get(),ui::Icon::Info,D2D1::RectF(28,620,48,640));}
    if(FAILED(canvas_->EndDraw())){brush_.Reset();canvas_.Reset();}
}
void SettingsView::DrawButton(const DRAWITEMSTRUCT& item,bool selected)const{
    const int id=item.CtlID;
    const bool header=id==IDCANCEL||id==IDC_ENGLISH||id==IDC_RUSSIAN;
    HDC dc=item.hDC;const int saved=SaveDC(dc);RECT r=item.rcItem;
    FillRect(dc,&r,header?background_:card_);
    SelectObject(dc,GetStockObject(DC_BRUSH));SelectObject(dc,GetStockObject(DC_PEN));
    const int unit=std::max(1L,(r.bottom-r.top)/18);
    const bool pressed=(item.itemState&ODS_SELECTED)!=0;
    if(id==IDC_STARTUP){
        InflateRect(&r,-4*unit,-3*unit);
        const int diameter=r.bottom-r.top;
        SetDCBrushColor(dc,selected?Ink:RGB(59,59,59));SetDCPenColor(dc,selected?Ink:RGB(59,59,59));
        RoundRect(dc,r.left,r.top,r.right,r.bottom,diameter,diameter);
        const int x=selected?r.right-diameter:r.left;
        SetDCBrushColor(dc,selected?RGB(28,28,28):Muted);
        Ellipse(dc,x+2*unit,r.top+2*unit,x+diameter-2*unit,r.bottom-2*unit);
    }else{
        const COLORREF fill=pressed||selected?RGB(65,65,65):id==IDC_HOTKEY_VALUE||id==IDCANCEL?RGB(28,28,28):RGB(51,51,51);
        SetDCBrushColor(dc,fill);SetDCPenColor(dc,selected?Ink:id==IDCANCEL?fill:RGB(59,59,59));
        RoundRect(dc,r.left,r.top,r.right,r.bottom,6*unit,6*unit);
        std::wstring label(GetWindowTextLengthW(item.hwndItem)+1,L'\0');
        GetWindowTextW(item.hwndItem,label.data(),static_cast<int>(label.size()));
        SelectObject(dc,reinterpret_cast<HFONT>(SendMessageW(item.hwndItem,WM_GETFONT,0,0)));
        SetBkMode(dc,TRANSPARENT);SetTextColor(dc,Ink);
        if(id==IDCANCEL){
            const int x=(r.left+r.right)/2,y=(r.top+r.bottom)/2;
            SetDCPenColor(dc,Ink);MoveToEx(dc,x-3*unit,y-3*unit,nullptr);LineTo(dc,x+3*unit+1,y+3*unit+1);
            MoveToEx(dc,x+3*unit,y-3*unit,nullptr);LineTo(dc,x-3*unit-1,y+3*unit+1);
        }else{
            if(id==IDC_HOTKEY_VALUE)r.left+=8*unit;
            DrawTextW(dc,label.c_str(),-1,&r,DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX|(id==IDC_HOTKEY_VALUE?DT_LEFT:DT_CENTER));
        }
    }
    if(item.itemState&ODS_FOCUS){
        RECT focus=item.rcItem;InflateRect(&focus,-3,-3);SetTextColor(dc,Ink);SetBkColor(dc,RGB(28,28,28));DrawFocusRect(dc,&focus);
    }
    RestoreDC(dc,saved);
}
}
