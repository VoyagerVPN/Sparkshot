#include "app.hpp"
#include "drawing.hpp"
#include "image.hpp"
#include "ui.hpp"
#include "save.hpp"
#include "fill.hpp"
#include "stroke.hpp"
#include "cursor.hpp"
#include "renderer.hpp"
#include <windowsx.h>
#include <commctrl.h>
#include <dwrite.h>
#include <shlobj.h>
#include <shellapi.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <stdexcept>
#include <cwctype>
#include <optional>
namespace sparkshot {
using Microsoft::WRL::ComPtr;
namespace {
constexpr UINT CaptureMessage=WM_APP+1, TrayMessage=WM_APP+2,FrameMessage=WM_APP+3;
constexpr const wchar_t* StartupKey=L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr int ToolCount=static_cast<int>(Tool::Fill)+1;
constexpr std::array<const wchar_t*,ToolCount> Names{L"Выбор",L"Карандаш",L"Линия",L"Стрелка",L"Прямоугольник",L"Ластик",L"Текст",L"Пикселизация",L"Нумерация",L"Пипетка",L"Эллипс",L"Заливка"};
constexpr std::array<unsigned,24> Swatches{0xff5757,0xff8a45,0xffc857,0x99dd58,0x54d69b,0x51cfd0,0x5db8ff,0x6788ff,0x9b83ff,0xd47dff,0xff82bd,0xffffff,0xad313c,0xae5830,0xaa8837,0x538844,0x278568,0x277d88,0x305b9e,0x414c9d,0x624995,0x8b4a9f,0xaa4b78,0x171d2a};
D2D1_RECT_F Normalize(D2D1_POINT_2F a,D2D1_POINT_2F b) {return D2D1::RectF(std::min(a.x,b.x),std::min(a.y,b.y),std::max(a.x,b.x),std::max(a.y,b.y));}
bool Inside(D2D1_RECT_F r,D2D1_POINT_2F p) {return p.x>=r.left&&p.x<=r.right&&p.y>=r.top&&p.y<=r.bottom;}
float Distance(D2D1_POINT_2F a,D2D1_POINT_2F b) {return std::hypot(a.x-b.x,a.y-b.y);}
bool IsBrush(Tool tool){return tool==Tool::Pen||tool==Tool::Eraser;}
float BrushRadius(float width){return width*.5f;}
}
struct App::Impl {
    HINSTANCE instance{}; HWND window{},edit{},hexEdit{};
    HFONT inputFont{};
    HFONT textInputFont{};
    HBRUSH inputBackground{CreateSolidBrush(RGB(32,39,53))};
    std::wstring command,status;
    std::filesystem::path base,folder,executable;
    std::wstring startupCommand;
    bool settings{},startupHere{};
    enum class ReturnView {None,Settings,Tray} captureReturn{ReturnView::None};
    RECT captureReturnBounds{};
    std::wstring captureReturnStatus;
    UINT captureKey{'S'},captureModifiers{MOD_CONTROL|MOD_ALT};
    bool choosingHotkey{};
    Image image;
    bool demo{},headless{},active{},selected{},dragging{},picker{},palette{},changingHex{},trayMenu{},hotkeyAvailable{true},selectedBefore{},annotationsDirty{true};
    D2D1_RECT_F paletteBounds{};
    float hue{},saturation{},value{};
    float brightnessHue{-1},brightnessSaturation{-1},textSize{24};
    ComPtr<ID2D1Bitmap> colorFieldBitmap,brightnessBitmap;
    ComPtr<IDWriteTextLayout> textLayout;
    bool textSelecting{},textDragging{},caretVisible{true};
    UINT32 textCaret{},textAnchor{};
    D2D1_POINT_2F textDragStart{},textOrigin{};
    int editingText{-1};
    int paletteDrag{},hover{-1};
    enum class Mode {None,Creating,Moving,Resizing,Drawing,MovingObject} mode{Mode::None};
    Tool tool{Tool::Select};
    bool snapShapes{},guides{};
    NativeCursors cursors;
    unsigned color{0xff5757}; float thickness{3};int pixelSize{12},wheelRemainder{};
    std::array<float,ToolCount> toolWidths{3,3,3,3,3,24,3,3,3,3,3,3};
    bool mouseInside{};
    using CustomColors=std::array<std::optional<unsigned>,12>;
    CustomColors customColors;
    D2D1_RECT_F selection{},originalSelection{};
    D2D1_POINT_2F down{},mouse{};
    int handle{-1},object{-1};
    Annotation preview,originalObject;
    StrokeRaster liveStroke;
    ComPtr<ID2D1Bitmap> liveStrokeBitmap;
    using Snapshot=std::vector<std::shared_ptr<const Annotation>>;
    Snapshot annotations;
    std::vector<Snapshot> undoStack,redoStack;
    ComPtr<ID2D1Factory1> factory;
    ComPtr<IDWriteFactory> textFactory;
    ComPtr<IDWriteTextFormat> font,smallFont;
    ComPtr<IDWriteTextFormat> settingsTitleFont,settingsSectionFont;
    Renderer renderer;
    ComPtr<ID2D1RenderTarget> target;
    ComPtr<ID2D1BitmapRenderTarget> sceneTarget;
    ComPtr<ID2D1Bitmap> sceneBitmap;
    bool framePending{},sceneDirty{true},frameSlotReady{},retryPresent{},captureShowPending{};
    unsigned demoGeneration{};
    RECT previousOverlay{};
    ComPtr<ID2D1Bitmap> background;
    ComPtr<ID2D1BitmapRenderTarget> annotationTarget;
    ComPtr<ID2D1Bitmap> annotationBitmap;
    ComPtr<ID2D1Bitmap> annotationSource;
    ComPtr<ID2D1StrokeStyle> lineStyle;
    ComPtr<ID2D1SolidColorBrush> brush;
    void RequestFrame(bool sceneChanged=true){
        sceneDirty|=sceneChanged;
        if(active&&!framePending){framePending=true;PostMessageW(window,FrameMessage,0,0);}
    }
    void DropRenderResources(bool resetDevice=false){
        colorFieldBitmap.Reset();brightnessBitmap.Reset();liveStrokeBitmap.Reset();background.Reset();annotationBitmap.Reset();annotationTarget.Reset();annotationSource.Reset();
        sceneBitmap.Reset();sceneTarget.Reset();brush.Reset();target.Reset();if(resetDevice)renderer.Reset();else renderer.ReleaseFrame();annotationsDirty=true;sceneDirty=true;frameSlotReady=false;retryPresent=false;previousOverlay={};
    }
    void Error(const std::exception& error) {
        const std::string description=error.what();
        const int length=MultiByteToWideChar(CP_UTF8,0,description.c_str(),-1,nullptr,0);
        std::wstring text(static_cast<size_t>(length),0);
        MultiByteToWideChar(CP_UTF8,0,description.c_str(),-1,text.data(),length);
        if(!text.empty()&&text.back()==0)text.pop_back();
        status=text;
        if(headless)SetWindowTextW(window,text.c_str());
        RequestFrame();
    }
    void LoadSettings() {
        wchar_t exe[32768]{},path[32768]{};const DWORD length=GetModuleFileNameW(headless?instance:nullptr,exe,32768);
        if(!length||length>=32768)throw std::runtime_error("Не удалось определить путь к Sparkshot.exe");
        executable=exe;base=executable.parent_path();
        if(headless){folder=base/L"test-output";return;}
        GetPrivateProfileStringW(L"Sparkshot",L"SaveFolder",L"",path,32768,(base/L"data"/L"settings.ini").c_str());
        if(*path){folder=path;if(folder.is_relative())folder=base/folder;}
        if(folder.empty()) {
            DWORD bytes=sizeof(path);
            if(RegGetValueW(HKEY_CURRENT_USER,L"Software\\Skillbrains\\Lightshot",L"LastSavedDir",RRF_RT_REG_SZ,nullptr,path,&bytes)==ERROR_SUCCESS&&*path)folder=path;
        }
        if(folder.empty())folder=base/L"Screenshots";
        const auto ini=(base/L"data"/L"settings.ini").wstring();
        const UINT storedKey=GetPrivateProfileIntW(L"Sparkshot",L"CaptureKey",'S',ini.c_str()),storedModifiers=GetPrivateProfileIntW(L"Sparkshot",L"CaptureModifiers",MOD_CONTROL|MOD_ALT,ini.c_str());
        if(storedKey>0&&storedKey<255&&(storedModifiers&~(MOD_CONTROL|MOD_ALT|MOD_SHIFT|MOD_WIN))==0){captureKey=storedKey;captureModifiers=storedModifiers;}
        wchar_t colors[256]{};GetPrivateProfileStringW(L"Sparkshot",L"CustomColors",L"",colors,256,(base/L"data"/L"settings.ini").c_str());
        std::wstring stored=colors;size_t start=0,slot=0;
        // Пустые поля сохраняют позиции слотов; старый непрерывный список читается без миграции.
        while(start<stored.size()&&slot<customColors.size()){const auto end=stored.find(L',',start);const auto item=stored.substr(start,end==std::wstring::npos?end:end-start);bool valid=item.size()==6;for(auto c:item)if(!iswxdigit(c))valid=false;if(valid)customColors[slot]=static_cast<unsigned>(wcstoul(item.c_str(),nullptr,16));++slot;if(end==std::wstring::npos)break;start=end+1;}
    }
    std::wstring StartupLine()const{return L"\""+executable.wstring()+L"\" --autostart";}
    void ReadStartup(){
        wchar_t stored[32768]{};DWORD bytes=sizeof(stored);
        const auto result=RegGetValueW(HKEY_CURRENT_USER,StartupKey,L"Sparkshot",RRF_RT_REG_SZ,nullptr,stored,&bytes);
        if(result!=ERROR_SUCCESS&&result!=ERROR_FILE_NOT_FOUND)throw std::runtime_error("Не удалось прочитать автозапуск Windows");
        startupCommand=result==ERROR_SUCCESS?stored:L"";startupHere=_wcsicmp(startupCommand.c_str(),StartupLine().c_str())==0;
    }
    void SetStartup(bool enabled){
        // В реестре меняется только запись Sparkshot и только по нажатию пользователя.
        const auto line=StartupLine();if(enabled&&line.size()>260)throw std::runtime_error("Путь слишком длинный для автозапуска Windows. Перенесите Sparkshot в более короткий путь.");
        HKEY key{};const auto opened=RegCreateKeyExW(HKEY_CURRENT_USER,StartupKey,0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr);
        if(opened!=ERROR_SUCCESS)throw std::runtime_error("Не удалось открыть настройки автозапуска Windows");
        const auto result=enabled?RegSetValueExW(key,L"Sparkshot",0,REG_SZ,reinterpret_cast<const BYTE*>(line.c_str()),static_cast<DWORD>((line.size()+1)*sizeof(wchar_t))):RegDeleteValueW(key,L"Sparkshot");RegCloseKey(key);
        if(result!=ERROR_SUCCESS&&!(result==ERROR_FILE_NOT_FOUND&&!enabled))throw std::runtime_error("Не удалось изменить автозапуск Windows");
        ReadStartup();status=enabled?L"Автозапуск сохранён для этой копии Sparkshot.":L"Автозапуск отключён.";RequestFrame();
    }
    std::wstring HotkeyName()const{
        std::wstring name;if(captureModifiers&MOD_CONTROL)name+=L"Ctrl+";if(captureModifiers&MOD_ALT)name+=L"Alt+";if(captureModifiers&MOD_SHIFT)name+=L"Shift+";if(captureModifiers&MOD_WIN)name+=L"Win+";
        if(captureKey==VK_SNAPSHOT)return name+L"Print Screen";
        const UINT scan=MapVirtualKeyW(captureKey,MAPVK_VK_TO_VSC);LONG flags=static_cast<LONG>(scan<<16);
        if(captureKey==VK_LEFT||captureKey==VK_RIGHT||captureKey==VK_UP||captureKey==VK_DOWN||captureKey==VK_HOME||captureKey==VK_END||captureKey==VK_PRIOR||captureKey==VK_NEXT||captureKey==VK_INSERT||captureKey==VK_DELETE||captureKey==VK_DIVIDE||captureKey==VK_NUMLOCK)flags|=1<<24;
        wchar_t label[128]{};if(GetKeyNameTextW(flags,label,128))return name+label;return name+L"VK "+std::to_wstring(captureKey);
    }
    void UpdateTrayTip(){NOTIFYICONDATAW icon{sizeof(icon)};icon.hWnd=window;icon.uID=1;icon.uFlags=NIF_TIP;const auto tip=L"Sparkshot — "+HotkeyName();wcsncpy_s(icon.szTip,tip.c_str(),_TRUNCATE);Shell_NotifyIconW(NIM_MODIFY,&icon);}
    void SetHotkey(UINT key,UINT modifiers){
        const bool wasRegistered=hotkeyAvailable;if(wasRegistered)UnregisterHotKey(window,1);
        if(!RegisterHotKey(window,1,modifiers|MOD_NOREPEAT,key)){
            hotkeyAvailable=wasRegistered&&RegisterHotKey(window,1,captureModifiers|MOD_NOREPEAT,captureKey)!=FALSE;
            status=L"Windows не разрешила это сочетание: оно может быть занято или зарезервировано. Прежнее сочетание сохранено.";RequestFrame();return;
        }
        // Обе части сочетания записываются одним обновлением секции INI.
        try{
            std::filesystem::create_directories(base/L"data");const auto ini=(base/L"data"/L"settings.ini").wstring();wchar_t section[4096]{};const DWORD count=GetPrivateProfileSectionW(L"Sparkshot",section,4096,ini.c_str());
            if(count>=4094)throw std::runtime_error("Секция настроек слишком велика для сохранения клавиши");
            std::wstring updated;for(const wchar_t* entry=section;*entry;entry+=wcslen(entry)+1){if(_wcsnicmp(entry,L"CaptureKey=",11)==0||_wcsnicmp(entry,L"CaptureModifiers=",17)==0)continue;updated+=entry;updated.push_back(0);}
            updated+=L"CaptureKey="+std::to_wstring(key);updated.push_back(0);updated+=L"CaptureModifiers="+std::to_wstring(modifiers);updated.push_back(0);updated.push_back(0);
            if(!WritePrivateProfileSectionW(L"Sparkshot",updated.c_str(),ini.c_str()))throw std::runtime_error("Не удалось сохранить клавишу рядом с EXE");
        }catch(...){UnregisterHotKey(window,1);hotkeyAvailable=wasRegistered&&RegisterHotKey(window,1,captureModifiers|MOD_NOREPEAT,captureKey)!=FALSE;throw;}
        captureKey=key;captureModifiers=modifiers;hotkeyAvailable=true;status=L"Клавиша захвата сохранена: "+HotkeyName();UpdateTrayTip();RequestFrame();
    }
    void HotkeyInput(WPARAM key){
        if(key==VK_ESCAPE){choosingHotkey=false;status.clear();RequestFrame();return;}
        if(key==VK_CONTROL||key==VK_LCONTROL||key==VK_RCONTROL||key==VK_MENU||key==VK_LMENU||key==VK_RMENU||key==VK_SHIFT||key==VK_LSHIFT||key==VK_RSHIFT||key==VK_LWIN||key==VK_RWIN)return;
        UINT modifiers=0;if(GetKeyState(VK_CONTROL)&0x8000)modifiers|=MOD_CONTROL;if(GetKeyState(VK_MENU)&0x8000)modifiers|=MOD_ALT;if(GetKeyState(VK_SHIFT)&0x8000)modifiers|=MOD_SHIFT;if((GetKeyState(VK_LWIN)|GetKeyState(VK_RWIN))&0x8000)modifiers|=MOD_WIN;
        choosingHotkey=false;SetHotkey(static_cast<UINT>(key),modifiers);
    }
    void ChooseFolder() {
        ComPtr<IFileDialog> dialog;
        if(FAILED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog))))return;
        DWORD options{};dialog->GetOptions(&options);dialog->SetOptions(options|FOS_PICKFOLDERS|FOS_FORCEFILESYSTEM);dialog->SetTitle(L"Папка для Screenshot_номер.png");
        ComPtr<IShellItem> current;if(SUCCEEDED(SHCreateItemFromParsingName(folder.c_str(),nullptr,IID_PPV_ARGS(&current))))dialog->SetFolder(current.Get());
        if(SUCCEEDED(dialog->Show(window))) {
            ComPtr<IShellItem> result;dialog->GetResult(&result);PWSTR path{};
            if(SUCCEEDED(result->GetDisplayName(SIGDN_FILESYSPATH,&path))) {
                folder=path;CoTaskMemFree(path);std::filesystem::create_directories(base/L"data");
                const auto relative=folder.lexically_relative(base);
                const auto stored=!relative.empty()&&*relative.begin()!=L".."?relative:folder;
                if(!WritePrivateProfileStringW(L"Sparkshot",L"SaveFolder",stored.c_str(),(base/L"data"/L"settings.ini").c_str()))throw std::runtime_error("Не удалось сохранить настройки рядом с EXE");
            }
        }
        RequestFrame();
    }
    void Hide() {
        ClosePalette();
        CancelText();colorFieldBitmap.Reset();brightnessBitmap.Reset();
        active=false;captureShowPending=false;trayMenu=false;settings=false;captureReturn=ReturnView::None;captureReturnStatus.clear();choosingHotkey=false;dragging=false;picker=false;snapShapes=false;guides=false;mode=Mode::None;ReleaseCapture();ShowWindow(window,SW_HIDE);
        liveStroke.Reset();liveStrokeBitmap.Reset();preview={};originalObject={};
        DropRenderResources();framePending=false;image={};annotations.clear();undoStack.clear();redoStack.clear();annotationsDirty=true;
    }
    void Capture() {
        const auto returnView=settings?ReturnView::Settings:trayMenu?ReturnView::Tray:captureReturn;auto returnBounds=captureReturnBounds;const auto returnStatus=settings||trayMenu?status:captureReturnStatus;
        if(settings||trayMenu)GetWindowRect(window,&returnBounds);
        // Сначала снимаем видимые окна, включая собственные; только затем освобождаем редактор.
        if(active)RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW|RDW_ALLCHILDREN);
        // BitBlt считывает рабочий стол; дополнительное ожидание DWM задерживало захват при нагрузке на GPU.
        auto captured=demo?DemoImage():CaptureDesktop();
        if(headless&&command.find(L"--vary-demo")!=std::wstring::npos&&++demoGeneration%2==0){
            // Разные изображения и размеры только для изолированной проверки смены снимка.
            constexpr int width=1000,height=700;std::vector<std::uint32_t> pixels(static_cast<size_t>(width)*height);
            for(int y=0;y<height;++y)for(int x=0;x<width;++x){const auto pixel=captured.pixels[static_cast<size_t>(y)*captured.width+x];pixels[static_cast<size_t>(y)*width+x]=(pixel&0xff00ff00)|((pixel&0xff)<<16)|((pixel>>16)&0xff);}
            captured.width=width;captured.height=height;captured.pixels=std::move(pixels);
        }
        Hide();image=std::move(captured);captureReturn=returnView;captureReturnBounds=returnBounds;captureReturnStatus=returnStatus;
        selected=false;selection={};annotations.clear();undoStack.clear();redoStack.clear();mode=Mode::None;status.clear();SelectTool(Tool::Select);
        if(headless){
            SetWindowPos(window,nullptr,0,0,image.width,image.height,SWP_NOACTIVATE|SWP_NOZORDER);
        }else if(demo) {
            SetWindowLongPtrW(window,GWL_STYLE,WS_POPUP|WS_CLIPCHILDREN);
            SetWindowPos(window,HWND_TOP,100,80,image.width,image.height,SWP_FRAMECHANGED|SWP_NOACTIVATE);
        } else {
            SetWindowLongPtrW(window,GWL_STYLE,WS_POPUP|WS_CLIPCHILDREN);
            SetWindowPos(window,HWND_TOPMOST,image.x,image.y,image.width,image.height,SWP_FRAMECHANGED|SWP_NOACTIVATE);
        }
        active=true;captureShowPending=!headless;guides=(GetKeyState(VK_TAB)&0x8000)!=0;RefreshCursor(true);
        if(!hotkeyAvailable)status=HotkeyName()+L" занято. Захват доступен через значок Sparkshot в трее; клавишу можно изменить в настройках.";
        // Пока новый кадр не отправлен, окно остаётся скрытым: DWM не показывает содержимое прошлого захвата.
        RequestFrame();Resources();frameSlotReady=WaitForSingleObject(renderer.FrameReady(),0)==WAIT_OBJECT_0;
        if(frameSlotReady)PaintFrame();
    }
    void FinishCapture(){
        const auto returnView=captureReturn;const auto bounds=captureReturnBounds;const auto previousStatus=captureReturnStatus;Hide();
        if(returnView==ReturnView::None)return;
        if(returnView==ReturnView::Settings)ShowSettings();else ShowTrayMenu();
        SetWindowPos(window,nullptr,bounds.left,bounds.top,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);status=previousStatus;RefreshCursor(true);RequestFrame();
    }
    void Resources() {
        if(target)return;
        RECT r{};GetClientRect(window,&r);
        renderer.Create(factory.Get(),window,r.right,r.bottom,false,headless&&command.find(L"--warp")!=std::wstring::npos);target=renderer.Target();
        target->CreateSolidColorBrush(D2D1::ColorF(0xffffff),&brush);
        target->CreateBitmap(D2D1::SizeU(image.width,image.height),image.pixels.data(),image.width*4,D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_IGNORE),96,96),&background);
    }
    void SetColor(ID2D1SolidColorBrush* paint,unsigned rgb,float alpha=1){paint->SetColor(D2D1::ColorF(rgb,alpha));}
    void Label(const std::wstring& label,D2D1_RECT_F r,unsigned rgb=0xeef3ff,bool compact=false){
        SetColor(brush.Get(),rgb);target->DrawTextW(label.c_str(),static_cast<UINT32>(label.size()),compact?smallFont.Get():font.Get(),r,brush.Get(),D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }
    ~Impl(){if(textInputFont)DeleteObject(textInputFont);if(inputFont)DeleteObject(inputFont);if(inputBackground)DeleteObject(inputBackground);}
    void Remember(){undoStack.push_back(annotations);if(undoStack.size()>128)undoStack.erase(undoStack.begin());redoStack.clear();annotationsDirty=true;}
    void Append(Annotation annotation){Remember();annotations.push_back(std::make_shared<const Annotation>(std::move(annotation)));}
    void Undo(){if(undoStack.empty()||dragging)return;redoStack.push_back(annotations);annotations=std::move(undoStack.back());undoStack.pop_back();annotationsDirty=true;RequestFrame();}
    void Redo(){if(redoStack.empty()||dragging)return;undoStack.push_back(annotations);annotations=std::move(redoStack.back());redoStack.pop_back();annotationsDirty=true;RequestFrame();}
    void Icon(ui::Icon icon,D2D1_RECT_F area,unsigned rgb=0xd5dfef){SetColor(brush.Get(),rgb);ui::DrawIcon(target.Get(),brush.Get(),icon,area);}
    void Panel(D2D1_RECT_F r){
        SetColor(brush.Get(),0x000000,.22f);auto shadow=r;shadow.left-=3;shadow.right+=3;shadow.top+=3;shadow.bottom+=7;target->FillRoundedRectangle(D2D1::RoundedRect(shadow,12,12),brush.Get());
        SetColor(brush.Get(),0x171e2b);target->FillRoundedRectangle(D2D1::RoundedRect(r,10,10),brush.Get());SetColor(brush.Get(),0x354157);target->DrawRoundedRectangle(D2D1::RoundedRect(r,10,10),brush.Get(),1);
    }
    void Button(D2D1_RECT_F r,bool on=false,bool enabled=true){
        if(on){SetColor(brush.Get(),0x403657);target->FillRoundedRectangle(D2D1::RoundedRect(r,6,6),brush.Get());SetColor(brush.Get(),0x9b83dd);target->DrawRoundedRectangle(D2D1::RoundedRect(r,6,6),brush.Get(),1);}
        else if(enabled&&Inside(r,mouse)){SetColor(brush.Get(),0x293548);target->FillRoundedRectangle(D2D1::RoundedRect(r,6,6),brush.Get());}
    }
    bool ToolsVisible()const{return selected&&mode!=Mode::Creating;}
    float ToolStep()const{return std::clamp((image.height-128.0f)/ToolCount,22.0f,32.0f);}
    D2D1_RECT_F ToolButton(int index)const{const auto r=Tools();const float y=r.top+6+index*ToolStep();return D2D1::RectF(r.left+6,y,r.right-6,y+ToolStep()-2);}
    D2D1_RECT_F UndoButton(bool redo)const{const auto r=Tools();const float y=r.top+ToolCount*ToolStep()+14+(redo?30.0f:0.0f);return D2D1::RectF(r.left+6,y,r.right-6,y+28);}
    D2D1_RECT_F ColorButton()const{const auto r=Tools();return D2D1::RectF(r.left+6,r.bottom-36,r.right-6,r.bottom-6);}
    int HoverAt(D2D1_POINT_2F p)const{if(!ToolsVisible()||dragging||palette)return -1;for(int i=0;i<ToolCount;++i)if(Inside(ToolButton(i),p))return i;if(Inside(UndoButton(false),p))return ToolCount;if(Inside(UndoButton(true),p))return ToolCount+1;if(Inside(ColorButton(),p))return ToolCount+2;return -1;}
    void PaintTools(){
        const auto panel=Tools(),actions=Actions();Panel(panel);Panel(actions);
        for(int i=0;i<ToolCount;++i){const auto r=ToolButton(i);Button(r,static_cast<int>(tool)==i);Icon(static_cast<ui::Icon>(i),D2D1::RectF(r.left+7,r.top+7,r.right-7,r.bottom-7),static_cast<int>(tool)==i?0xd9c8ff:0xc9d5e9);}
        SetColor(brush.Get(),0x303d51);target->DrawLine(D2D1::Point2F(panel.left+10,panel.top+ToolCount*ToolStep()+10),D2D1::Point2F(panel.right-10,panel.top+ToolCount*ToolStep()+10),brush.Get());
        for(bool redo:{false,true}){auto r=UndoButton(redo);const bool enabled=redo?!redoStack.empty():!undoStack.empty();Button(r,false,enabled);Icon(redo?ui::Icon::Redo:ui::Icon::Undo,D2D1::RectF(r.left+8,r.top+4,r.right-8,r.bottom-4),enabled?0xc9d5e9:0x586477);}
        const auto colors=ColorButton();Button(colors,palette);const auto center=D2D1::Point2F((colors.left+colors.right)/2,(colors.top+colors.bottom)/2);
        SetColor(brush.Get(),color);target->FillEllipse(D2D1::Ellipse(center,9,9),brush.Get());SetColor(brush.Get(),0xb8c3d4);target->DrawEllipse(D2D1::Ellipse(center,9,9),brush.Get(),1);
        const std::array<D2D1_RECT_F,4> buttons{D2D1::RectF(actions.left+5,actions.top+5,actions.left+140,actions.bottom-5),D2D1::RectF(actions.left+142,actions.top+5,actions.left+278,actions.bottom-5),D2D1::RectF(actions.left+280,actions.top+5,actions.left+380,actions.bottom-5),D2D1::RectF(actions.left+385,actions.top+5,actions.right-5,actions.bottom-5)};
        for(const auto r:buttons)Button(r);
        for(int i=0;i<3;++i){auto r=buttons[i];Icon(i==0?ui::Icon::Copy:i==1?ui::Icon::Save:ui::Icon::Folder,D2D1::RectF(r.left+8,r.top+7,r.left+28,r.bottom-7));Label(i==0?L"Копировать":i==1?L"Сохранить":L"Папка",D2D1::RectF(r.left+37,r.top+7,r.right,r.bottom),0xe0e8f5,true);}
        const auto close=buttons[3];Icon(ui::Icon::Close,D2D1::RectF(close.left+7,close.top+7,close.right-7,close.bottom-7));
        if(hover>=0){const auto hint=hover<ToolCount?Names[hover]:hover==ToolCount?L"Отменить · Ctrl+Z":hover==ToolCount+1?L"Повторить · Ctrl+Y":L"Цвет";const float x=std::clamp(panel.left-205,8.0f,std::max(8.0f,image.width-215.0f)),y=std::clamp(mouse.y-18,8.0f,std::max(8.0f,image.height-42.0f));Panel(D2D1::RectF(x,y,x+198,y+34));Label(hint,D2D1::RectF(x+10,y+8,x+190,y+32),0xe0e8f5,true);}
    }
    D2D1_RECT_F ColorField()const{return D2D1::RectF(paletteBounds.left+12,paletteBounds.top+54,paletteBounds.left+170,paletteBounds.top+202);}
    D2D1_RECT_F BrightnessBar()const{return D2D1::RectF(paletteBounds.left+186,paletteBounds.top+54,paletteBounds.left+202,paletteBounds.top+202);}
    bool OnBrightness(D2D1_POINT_2F point)const{auto r=BrightnessBar();r.left-=6;r.right+=6;return Inside(r,point);}
    D2D1_RECT_F PaletteClose()const{return D2D1::RectF(paletteBounds.right-28,paletteBounds.top+5,paletteBounds.right-6,paletteBounds.top+27);}
    D2D1_RECT_F ColorCell(int index,bool custom=false)const{const float x=paletteBounds.left+12+(index%(custom?6:8))*(custom?32.0f:24.0f),y=paletteBounds.top+(custom?330:214)+(index/(custom?6:8))*(custom?26.0f:22.0f);return D2D1::RectF(x,y,x+(custom?26:20),y+(custom?20:18));}
    D2D1_RECT_F ColorDelete(int index)const{const auto r=ColorCell(index,true);return D2D1::RectF(r.right-7,r.top-3,r.right+3,r.top+7);}
    void SyncHex(){if(!hexEdit)return;wchar_t hex[8]{};swprintf_s(hex,L"%06X",color);changingHex=true;SetWindowTextW(hexEdit,hex);changingHex=false;}
    void ReadHex(){if(!hexEdit||changingHex)return;wchar_t hex[8]{};GetWindowTextW(hexEdit,hex,8);if(wcslen(hex)!=6)return;for(int i=0;i<6;++i)if(!iswxdigit(hex[i]))return;color=static_cast<unsigned>(wcstoul(hex,nullptr,16));ui::RgbToHsv(color,hue,saturation,value);RequestFrame();}
    void ClosePalette(){if(hexEdit){DestroyWindow(hexEdit);hexEdit=nullptr;}palette=false;if(paletteDrag){paletteDrag=0;ReleaseCapture();}RefreshCursor(true);RequestFrame();}
    static LRESULT CALLBACK HexProc(HWND hwnd,UINT message,WPARAM wparam,LPARAM lparam,UINT_PTR,DWORD_PTR data){
        auto* self=reinterpret_cast<Impl*>(data);
        if((message==WM_KEYDOWN||message==WM_KEYUP)&&wparam==VK_TAB){self->Modifier(wparam,message==WM_KEYDOWN);return 0;}
        if(message==WM_CHAR&&wparam==VK_TAB)return 0;
        if(message==WM_KEYDOWN&&(wparam==VK_ESCAPE||wparam==VK_RETURN)){self->ReadHex();self->ClosePalette();SetFocus(self->window);return 0;}
        if(message==WM_MOUSEWHEEL){self->Wheel(GET_WHEEL_DELTA_WPARAM(wparam),GET_KEYSTATE_WPARAM(wparam));self->RefreshCursor();return 0;}
        return DefSubclassProc(hwnd,message,wparam,lparam);
    }
    void OpenPalette(D2D1_POINT_2F at){
        CommitText();ClosePalette();if(!selected)return;hover=-1;palette=true;ui::RgbToHsv(color,hue,saturation,value);
        const float x=std::clamp(at.x,8.0f,std::max(8.0f,image.width-228.0f)),y=std::clamp(at.y,8.0f,std::max(8.0f,image.height-398.0f));paletteBounds=D2D1::RectF(x,y,x+220,y+390);
        hexEdit=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL,static_cast<int>(x+31),static_cast<int>(y+283),76,22,window,nullptr,instance,nullptr);
        SendMessageW(hexEdit,WM_SETFONT,reinterpret_cast<WPARAM>(inputFont),TRUE);SendMessageW(hexEdit,EM_SETLIMITTEXT,6,0);SetWindowSubclass(hexEdit,HexProc,1,reinterpret_cast<DWORD_PTR>(this));SyncHex();SetFocus(window);RequestFrame();
    }
    void PaletteValue(D2D1_POINT_2F p){if(paletteDrag==2){const auto r=BrightnessBar();value=1-std::clamp((p.y-r.top)/(r.bottom-r.top),0.0f,1.0f);}else ui::ColorFieldPosition(ColorField(),p,hue,saturation);color=ui::HsvToRgb(hue,saturation,value);SyncHex();RequestFrame();}
    void PaletteDown(D2D1_POINT_2F p){
        if(!Inside(paletteBounds,p)){ClosePalette();SetFocus(window);return;}
        if(Inside(PaletteClose(),p)){ClosePalette();return;}
        if(Inside(ColorField(),p)||OnBrightness(p)){paletteDrag=OnBrightness(p)?2:1;if(!headless)SetCapture(window);PaletteValue(p);return;}
        for(int i=0;i<12;++i)if(customColors[i]&&Inside(ColorDelete(i),p)){auto colors=customColors;colors[i].reset();StoreCustomColors(std::move(colors));return;}
        for(int i=0;i<24;++i)if(Inside(ColorCell(i),p)){color=Swatches[i];ui::RgbToHsv(color,hue,saturation,value);SyncHex();RequestFrame();return;}
        for(int i=0;i<static_cast<int>(customColors.size());++i)if(Inside(ColorCell(i,true),p)){
            if(!customColors[i]){ReadHex();auto colors=customColors;colors[i]=color;StoreCustomColors(std::move(colors));}
            else{color=*customColors[i];ui::RgbToHsv(color,hue,saturation,value);SyncHex();RequestFrame();}return;
        }
    }
    void StoreCustomColors(CustomColors colors){
        std::wstring list;for(size_t i=0;i<colors.size();++i){if(i)list+=L",";if(colors[i]){wchar_t hex[8]{};swprintf_s(hex,L"%06X",*colors[i]);list+=hex;}}
        std::filesystem::create_directories(base/L"data");if(!WritePrivateProfileStringW(L"Sparkshot",L"CustomColors",list.c_str(),(base/L"data"/L"settings.ini").c_str()))throw std::runtime_error("Не удалось сохранить свои цвета рядом с EXE");customColors=std::move(colors);RequestFrame();
    }
    void PreparePalette(){
        if(!palette)return;const auto properties=D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_IGNORE),96,96);
        // Поле не зависит от текущего цвета; при движении меняется только узкая шкала яркости.
        if(!colorFieldBitmap){const auto pixels=ui::ColorFieldPixels(158,148);if(FAILED(target->CreateBitmap(D2D1::SizeU(158,148),pixels.data(),158*4,properties,&colorFieldBitmap)))throw std::runtime_error("Не удалось создать цветовое поле");}
        if(brightnessBitmap&&brightnessHue==hue&&brightnessSaturation==saturation)return;
        const auto pixels=ui::BrightnessPixels(16,148,hue,saturation);HRESULT result=S_OK;
        if(brightnessBitmap)result=brightnessBitmap->CopyFromMemory(nullptr,pixels.data(),16*4);else result=target->CreateBitmap(D2D1::SizeU(16,148),pixels.data(),16*4,properties,&brightnessBitmap);
        if(FAILED(result))throw std::runtime_error("Не удалось обновить шкалу яркости");brightnessHue=hue;brightnessSaturation=saturation;
    }
    void PaintPalette(){
        const auto r=paletteBounds;Panel(r);Label(L"Цвет",D2D1::RectF(r.left+12,r.top+8,r.left+110,r.top+29),0xe2e9f6,true);Button(PaletteClose());Icon(ui::Icon::Close,D2D1::RectF(r.right-24,r.top+9,r.right-10,r.top+23));
        Label(L"Оттенок",D2D1::RectF(r.left+12,r.top+32,r.left+104,r.top+52),0x9cacbf,true);Label(L"Яркость",D2D1::RectF(r.left+151,r.top+32,r.right-8,r.top+52),0x9cacbf,true);ui::DrawColorField(target.Get(),brush.Get(),colorFieldBitmap.Get(),ColorField(),hue,saturation);
        const auto bar=BrightnessBar();if(brightnessBitmap)target->DrawBitmap(brightnessBitmap.Get(),bar);const float y=bar.top+(1-value)*(bar.bottom-bar.top);SetColor(brush.Get(),0x080d16);target->DrawLine(D2D1::Point2F(bar.left-3,y),D2D1::Point2F(bar.right+3,y),brush.Get(),5);SetColor(brush.Get(),0xffffff);target->DrawLine(D2D1::Point2F(bar.left-3,y),D2D1::Point2F(bar.right+3,y),brush.Get(),2);
        for(int i=0;i<24;++i){const auto cell=ColorCell(i);SetColor(brush.Get(),Swatches[i]);target->FillRoundedRectangle(D2D1::RoundedRect(cell,3,3),brush.Get());if(color==Swatches[i]){SetColor(brush.Get(),0xffffff);target->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(cell.left-1,cell.top-1,cell.right+1,cell.bottom+1),4,4),brush.Get(),1);}}
        SetColor(brush.Get(),0x202735);target->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(r.left+12,r.top+280,r.left+114,r.top+308),4,4),brush.Get());Label(L"#",D2D1::RectF(r.left+18,r.top+284,r.left+31,r.top+306),0x8797b0,true);SetColor(brush.Get(),color);target->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(r.left+124,r.top+280,r.right-12,r.top+308),4,4),brush.Get());
        Label(L"Свои цвета",D2D1::RectF(r.left+12,r.top+309,r.right-12,r.top+328),0x9cacbf,true);
        for(int i=0;i<12;++i){const auto cell=ColorCell(i,true);Button(cell);if(customColors[i]){SetColor(brush.Get(),*customColors[i]);target->FillRoundedRectangle(D2D1::RoundedRect(cell,3,3),brush.Get());if(color==*customColors[i]){SetColor(brush.Get(),0xffffff);target->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(cell.left-1,cell.top-1,cell.right+1,cell.bottom+1),4,4),brush.Get(),1);}
            const auto remove=ColorDelete(i);SetColor(brush.Get(),Inside(remove,mouse)?0x8b3347:0x202735);target->FillEllipse(D2D1::Ellipse(D2D1::Point2F((remove.left+remove.right)/2,(remove.top+remove.bottom)/2),6,6),brush.Get());Icon(ui::Icon::Close,D2D1::RectF(remove.left+2,remove.top+2,remove.right-2,remove.bottom-2),0xffffff);
        }else{SetColor(brush.Get(),0x354157);target->DrawRoundedRectangle(D2D1::RoundedRect(cell,3,3),brush.Get(),1);Icon(ui::Icon::Add,D2D1::RectF(cell.left+4,cell.top+3,cell.right-4,cell.bottom-3),Inside(cell,mouse)?0xf0eaff:0x8191a9);}}
    }
    std::array<D2D1_POINT_2F,8> Handles()const{
        const float x=(selection.left+selection.right)/2,y=(selection.top+selection.bottom)/2;
        return {{{selection.left,selection.top},{x,selection.top},{selection.right,selection.top},{selection.right,y},{selection.right,selection.bottom},{x,selection.bottom},{selection.left,selection.bottom},{selection.left,y}}};
    }
    void PaintGuides(){
        if(!guides)return;const auto handles=Handles();
        constexpr std::array<std::array<int,2>,4> pairs{{{1,5},{3,7},{0,4},{2,6}}};
        // Это слой интерфейса: направляющие не входят в аннотации, историю и экспорт.
        for(int pass=0;pass<2;++pass){SetColor(brush.Get(),pass?0xe5f0ff:0x06090f,pass?.8f:.5f);for(const auto& pair:pairs)target->DrawLine(handles[pair[0]],handles[pair[1]],brush.Get(),pass?1.0f:3.0f);}
    }
    D2D1_RECT_F Tools()const{
        const float height=ToolCount*ToolStep()+108;float x=selection.right+12;if(x+48>image.width-8)x=std::max(8.0f,selection.left-60);
        const float y=std::clamp(selection.top,10.0f,std::max(10.0f,image.height-height-10));
        return D2D1::RectF(x,y,x+48,y+height);
    }
    D2D1_RECT_F Actions()const{
        const float x=std::clamp(selection.left,10.0f,std::max(10.0f,image.width-434.0f));float y=selection.bottom+12;
        if(y+46>image.height-8)y=std::max(8.0f,selection.top-58);
        return D2D1::RectF(x,y,x+424,y+44);
    }
    void SelectTool(Tool next){toolWidths[static_cast<size_t>(tool)]=thickness;tool=next;thickness=toolWidths[static_cast<size_t>(tool)];snapShapes=(GetKeyState(VK_SHIFT)&0x8000)!=0;}
    void DrawStrokeMask(ID2D1RenderTarget* canvas,ID2D1SolidColorBrush* paint,Tool strokeTool,ID2D1Bitmap* mask,D2D1_RECT_F destination,D2D1_RECT_F maskArea,ID2D1Bitmap* source){
        if(!mask)return;
        ComPtr<ID2D1BitmapBrush> original;
        if(strokeTool==Tool::Eraser){if(source)canvas->CreateBitmapBrush(source,D2D1::BitmapBrushProperties(D2D1_EXTEND_MODE_CLAMP,D2D1_EXTEND_MODE_CLAMP,D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR),&original);if(!original)return;}
        // Сглаживание уже содержится в альфа-маске; FillOpacityMask требует ALIASED.
        const auto antialias=canvas->GetAntialiasMode();canvas->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
        canvas->FillOpacityMask(mask,original?static_cast<ID2D1Brush*>(original.Get()):paint,D2D1_OPACITY_MASK_CONTENT_GRAPHICS,&destination,&maskArea);
        canvas->SetAntialiasMode(antialias);
    }
    void UploadLiveStroke(){
        if(mode!=Mode::Drawing||!IsBrush(preview.tool)||liveStroke.Pixels().empty())return;
        HRESULT hr=S_OK;
        if(!liveStrokeBitmap)hr=target->CreateBitmap(D2D1::SizeU(image.width,image.height),liveStroke.Pixels().data(),image.width*4,D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),96,96),&liveStrokeBitmap);
        else {const auto dirty=liveStroke.Dirty();if(dirty.left<dirty.right&&dirty.top<dirty.bottom)hr=liveStrokeBitmap->CopyFromMemory(&dirty,liveStroke.Pixels().data()+static_cast<size_t>(dirty.top)*image.width+dirty.left,image.width*4);}
        if(FAILED(hr))throw std::runtime_error("Не удалось обновить маску кисти");liveStroke.Uploaded();
    }
    void DrawLiveStroke(){
        const auto bounds=liveStroke.Bounds();const auto area=D2D1::RectF(static_cast<float>(bounds.left),static_cast<float>(bounds.top),static_cast<float>(bounds.right),static_cast<float>(bounds.bottom));
        SetColor(brush.Get(),preview.color);
        DrawStrokeMask(target.Get(),brush.Get(),preview.tool,liveStrokeBitmap.Get(),area,area,background.Get());
    }
    void DrawAnnotation(ID2D1RenderTarget* canvas,ID2D1SolidColorBrush* paint,const Annotation& a,ID2D1Bitmap* source){
        if(a.points.empty())return;SetColor(paint,a.color);
        const auto p=a.points.front(),q=a.points.back();const float width=a.width;
        if(IsBrush(a.tool)){
            if(!a.stroke)return;const auto& stroke=*a.stroke;ComPtr<ID2D1Bitmap> mask;
            if(FAILED(canvas->CreateBitmap(D2D1::SizeU(stroke.width,stroke.height),stroke.pixels.data(),stroke.width*4,D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),96,96),&mask)))return;
            const float x=p.x+stroke.offsetX,y=p.y+stroke.offsetY;
            DrawStrokeMask(canvas,paint,a.tool,mask.Get(),D2D1::RectF(x,y,x+stroke.width,y+stroke.height),D2D1::RectF(0,0,static_cast<float>(stroke.width),static_cast<float>(stroke.height)),source);
        }else if(a.tool==Tool::Line||a.tool==Tool::Arrow){
            if(a.tool==Tool::Line)canvas->DrawLine(p,q,paint,width,lineStyle.Get());
            else {const float length=Distance(p,q);if(length<.01f)return;const float dx=(q.x-p.x)/length,dy=(q.y-p.y)/length,head=std::min(std::max(12.0f,width*4),length*.8f),half=head*.4f;const auto headBase=D2D1::Point2F(q.x-dx*head,q.y-dy*head);
                canvas->DrawLine(p,D2D1::Point2F(q.x-dx*head*.65f,q.y-dy*head*.65f),paint,width);
                ComPtr<ID2D1PathGeometry> geometry;ComPtr<ID2D1GeometrySink> sink;if(SUCCEEDED(factory->CreatePathGeometry(&geometry))&&SUCCEEDED(geometry->Open(&sink))){sink->BeginFigure(q,D2D1_FIGURE_BEGIN_FILLED);sink->AddLine(D2D1::Point2F(headBase.x-dy*half,headBase.y+dx*half));sink->AddLine(D2D1::Point2F(headBase.x+dy*half,headBase.y-dx*half));sink->EndFigure(D2D1_FIGURE_END_CLOSED);sink->Close();canvas->FillGeometry(geometry.Get(),paint);}}
        }else if(a.tool==Tool::Rectangle)canvas->DrawRectangle(Normalize(p,q),paint,width);
        else if(a.tool==Tool::Fill&&a.fill){ComPtr<ID2D1Bitmap> bitmap;const auto& fill=*a.fill;if(SUCCEEDED(canvas->CreateBitmap(D2D1::SizeU(fill.width,fill.height),fill.pixels.data(),fill.width*4,D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),96,96),&bitmap)))canvas->DrawBitmap(bitmap.Get(),D2D1::RectF(p.x,p.y,p.x+fill.width,p.y+fill.height),1,D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);}
        else if(a.tool==Tool::Ellipse){const auto r=Normalize(p,q);canvas->DrawEllipse(D2D1::Ellipse(D2D1::Point2F((r.left+r.right)/2,(r.top+r.bottom)/2),(r.right-r.left)/2,(r.bottom-r.top)/2),paint,width);}
        else if(a.tool==Tool::Text||a.tool==Tool::Number){
            if(a.tool==Tool::Text){const auto layout=CreateTextLayout(a);if(layout){const auto previous=canvas->GetTextAntialiasMode();canvas->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);canvas->DrawTextLayout(p,layout.Get(),paint);canvas->SetTextAntialiasMode(previous);}return;}
            ComPtr<IDWriteTextFormat> format;const float size=18;
            textFactory->CreateTextFormat(L"Segoe UI",nullptr,DWRITE_FONT_WEIGHT_SEMI_BOLD,DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,size,L"ru-ru",&format);
            if(a.tool==Tool::Number){canvas->FillEllipse(D2D1::Ellipse(p,15,15),paint);SetColor(paint,0xffffff);format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);}
            canvas->DrawTextW(a.text.c_str(),static_cast<UINT32>(a.text.size()),format.Get(),a.tool==Tool::Number?D2D1::RectF(p.x-15,p.y-15,p.x+15,p.y+15):D2D1::RectF(p.x,p.y,p.x+800,p.y+250),paint);
        }else if(a.tool==Tool::Pixelate){
            const auto r=Normalize(p,q);const int block=std::max(2,a.pixelSize);
            const int left=std::clamp(static_cast<int>(r.left),0,image.width),top=std::clamp(static_cast<int>(r.top),0,image.height),right=std::clamp(static_cast<int>(std::ceil(r.right)),left,image.width),bottom=std::clamp(static_cast<int>(std::ceil(r.bottom)),top,image.height);
            const int w=right-left,h=bottom-top;if(w==0||h==0)return;std::vector<std::uint32_t> pixels(static_cast<size_t>(w)*h);
            for(int y=top;y<bottom;y+=block)
                for(int x=left;x<right;x+=block){
                    unsigned rr=0,gg=0,bb=0,count=0;
                    for(int sy=y;sy<std::min(bottom,y+block);++sy)for(int sx=x;sx<std::min(right,x+block);++sx){
                        const auto pixel=image.pixels[static_cast<size_t>(sy)*image.width+sx];rr+=(pixel>>16)&255;gg+=(pixel>>8)&255;bb+=pixel&255;++count;}
                    if(count){const std::uint32_t rgb=0xff000000|((rr/count)<<16)|((gg/count)<<8)|(bb/count);for(int sy=y;sy<std::min(bottom,y+block);++sy){const size_t offset=static_cast<size_t>(sy-top)*w+x-left;std::fill(pixels.begin()+offset,pixels.begin()+offset+std::min(block,right-x),rgb);}}
                }
            ComPtr<ID2D1Bitmap> bitmap;if(SUCCEEDED(canvas->CreateBitmap(D2D1::SizeU(w,h),pixels.data(),w*4,D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_IGNORE),96,96),&bitmap)))canvas->DrawBitmap(bitmap.Get(),D2D1::RectF(static_cast<float>(left),static_cast<float>(top),static_cast<float>(right),static_cast<float>(bottom)),1,D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
        }
    }
    void CacheAnnotations(){
        if(!annotationsDirty)return;
        if(annotations.empty()){annotationBitmap.Reset();annotationTarget.Reset();annotationSource.Reset();annotationsDirty=false;return;}
        if(!annotationTarget&&FAILED(target->CreateCompatibleRenderTarget(D2D1::SizeF(static_cast<float>(image.width),static_cast<float>(image.height)),&annotationTarget)))return;
        // Совместимые поверхности одного устройства используют исходный снимок без второй копии в VRAM.
        if(!annotationSource)annotationSource=background;
        ComPtr<ID2D1SolidColorBrush> paint;annotationTarget->CreateSolidColorBrush(D2D1::ColorF(color),&paint);if(!paint)return;
        // Законченные фигуры рисуются только после изменения документа; движение мыши меняет лишь предпросмотр.
        annotationTarget->BeginDraw();annotationTarget->Clear(D2D1::ColorF(0,0.0f));
        for(size_t i=0;i<annotations.size();++i)if(!edit||editingText!=static_cast<int>(i))DrawAnnotation(annotationTarget.Get(),paint.Get(),*annotations[i],annotationSource.Get());
        if(SUCCEEDED(annotationTarget->EndDraw())){annotationBitmap.Reset();annotationTarget->GetBitmap(&annotationBitmap);annotationsDirty=false;}
    }
    void PaintScene(){
        if(!sceneDirty&&sceneBitmap)return;
        if(!sceneTarget&&FAILED(target->CreateCompatibleRenderTarget(D2D1::SizeF(static_cast<float>(image.width),static_cast<float>(image.height)),&sceneTarget)))throw std::runtime_error("Не удалось кэшировать редактор");
        const auto screenTarget=target;target=sceneTarget.Get();
        target->BeginDraw();target->SetTransform(D2D1::Matrix3x2F::Identity());
        target->DrawBitmap(background.Get());
        if(mode==Mode::MovingObject||annotationsDirty){for(size_t i=0;i<annotations.size();++i)if(!edit||editingText!=static_cast<int>(i))DrawAnnotation(target.Get(),brush.Get(),mode==Mode::MovingObject&&object==static_cast<int>(i)?preview:*annotations[i],background.Get());}
        else if(annotationBitmap)target->DrawBitmap(annotationBitmap.Get());
        if(mode==Mode::Drawing){if(IsBrush(preview.tool))DrawLiveStroke();else DrawAnnotation(target.Get(),brush.Get(),preview,background.Get());}
        SetColor(brush.Get(),0x080808,.56f);
        if(selected){
            target->FillRectangle(D2D1::RectF(0,0,static_cast<float>(image.width),selection.top),brush.Get());
            target->FillRectangle(D2D1::RectF(0,selection.bottom,static_cast<float>(image.width),static_cast<float>(image.height)),brush.Get());
            target->FillRectangle(D2D1::RectF(0,selection.top,selection.left,selection.bottom),brush.Get());target->FillRectangle(D2D1::RectF(selection.right,selection.top,static_cast<float>(image.width),selection.bottom),brush.Get());
            PaintGuides();SetColor(brush.Get(),0xd9e8ff);target->DrawRectangle(selection,brush.Get(),1);
            for(auto p:Handles()){SetColor(brush.Get(),0xffffff);target->FillRectangle(D2D1::RectF(p.x-4,p.y-4,p.x+4,p.y+4),brush.Get());SetColor(brush.Get(),0x579bff);target->FillRectangle(D2D1::RectF(p.x-3,p.y-3,p.x+3,p.y+3),brush.Get());}
            {
                if(ToolsVisible())PaintTools();
                Label(std::to_wstring(static_cast<int>(selection.right-selection.left))+L" × "+std::to_wstring(static_cast<int>(selection.bottom-selection.top)),D2D1::RectF(selection.left+8,std::max(4.0f,selection.top-29),selection.left+180,selection.top+3),0xffffff,true);
            }
        }else{
            target->FillRectangle(D2D1::RectF(0,0,static_cast<float>(image.width),static_cast<float>(image.height)),brush.Get());
            Icon(ui::Icon::Spark,D2D1::RectF(28,26,52,50),0xc4acff);Label(L"Sparkshot",D2D1::RectF(64,24,300,62));Label(L"Выделите область · Esc — закрыть",D2D1::RectF(28,65,550,100),0xc2d0e6,true);
        }
        if(mode!=Mode::Creating){Label(ToolsVisible()?std::wstring(Names[static_cast<int>(tool)])+L" · Shift: форма · колесо: размер · Alt: пипетка · Ctrl+Z / Ctrl+Y / Ctrl+Shift+Z":L"Нативный захват · "+HotkeyName(),D2D1::RectF(28,static_cast<float>(image.height-33),static_cast<float>(image.width-20),static_cast<float>(image.height)),0xc2d0e6,true);
        Button(CloseButton());Icon(ui::Icon::Close,D2D1::RectF(static_cast<float>(image.width-36),20,static_cast<float>(image.width-20),36));}
        if(mode!=Mode::Creating&&!status.empty()){Panel(D2D1::RectF(28,110,static_cast<float>(std::min(image.width-28,850)),166));Label(status,D2D1::RectF(42,128,static_cast<float>(std::min(image.width-42,836)),162),0xffc857,true);}
        if(edit)PaintTextEditor();if(palette)PaintPalette();
        const HRESULT result=target->EndDraw();target=screenTarget;
        if(FAILED(result))throw std::runtime_error("Не удалось отрисовать редактор");
        sceneBitmap.Reset();if(FAILED(sceneTarget->GetBitmap(&sceneBitmap)))throw std::runtime_error("Не удалось получить кэш редактора");sceneDirty=false;
    }
    void PaintPicker(){
        if(PickerAtMouse()){
            const int x=std::clamp(static_cast<int>(mouse.x),0,image.width-1),y=std::clamp(static_cast<int>(mouse.y),0,image.height-1);
            const float lx=std::clamp(mouse.x+24,8.0f,image.width-136.0f),ly=std::clamp(mouse.y+24,8.0f,image.height-161.0f);
            SetColor(brush.Get(),0x181e29);target->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(lx-3,ly-3,lx+129,ly+152),5,5),brush.Get());
            // 21×21 пиксель: выбранный всегда в центре, у краёв оставляем пустые клетки вместо растяжения.
            const int left=std::max(0,x-10),top=std::max(0,y-10),right=std::min(image.width,x+11),bottom=std::min(image.height,y+11);
            target->DrawBitmap(background.Get(),D2D1::RectF(lx+(left-x+10)*6,ly+(top-y+10)*6,lx+(right-x+10)*6,ly+(bottom-y+10)*6),1,D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR,D2D1::RectF(static_cast<float>(left),static_cast<float>(top),static_cast<float>(right),static_cast<float>(bottom)));
            SetColor(brush.Get(),0x06090f);target->DrawRectangle(D2D1::RectF(lx+60,ly+60,lx+66,ly+66),brush.Get(),3);
            SetColor(brush.Get(),0xffffff);target->DrawRectangle(D2D1::RectF(lx+60,ly+60,lx+66,ly+66),brush.Get(),1);
            wchar_t hex[12]{};swprintf_s(hex,L"#%06X",image.pixels[static_cast<size_t>(y)*image.width+x]&0xffffff);
            Label(hex,D2D1::RectF(lx+15,ly+128,lx+126,ly+150),0xffffff,true);
        }
    }
    RECT OverlayBounds()const{
        if(!ToolPointerAtMouse())return {};
        const float radius=std::max(2.0f,(tool==Tool::Pixelate?pixelSize:thickness)*.5f);
        RECT area{static_cast<LONG>(std::floor(mouse.x-radius-14)),static_cast<LONG>(std::floor(mouse.y-radius-26)),static_cast<LONG>(std::ceil(mouse.x+radius+100)),static_cast<LONG>(std::ceil(mouse.y+radius+44))};
        if(PickerAtMouse()){
            const float x=std::clamp(mouse.x+24,8.0f,image.width-136.0f),y=std::clamp(mouse.y+24,8.0f,image.height-161.0f);
            RECT lens{static_cast<LONG>(x)-5,static_cast<LONG>(y)-5,static_cast<LONG>(x)+132,static_cast<LONG>(y)+155};UnionRect(&area,&area,&lens);
        }
        const RECT bounds{0,0,image.width,image.height};IntersectRect(&area,&area,&bounds);return area;
    }
    void PresentFrame(HRESULT drawing){
        const HRESULT result=SUCCEEDED(drawing)?renderer.Present():drawing;
        frameSlotReady=false;
        if(result==DXGI_ERROR_WAS_STILL_DRAWING){retryPresent=true;RequestFrame(false);return;}
        retryPresent=false;
        if(FAILED(result)){DropRenderResources(true);RequestFrame();if(headless)throw std::runtime_error("Ошибка вывода кадра: "+std::to_string(static_cast<unsigned>(result)));}
        else if(captureShowPending){captureShowPending=false;ShowWindow(window,SW_SHOWNOACTIVATE);SetForegroundWindow(window);SetFocus(window);RefreshCursor(true);RequestFrame(false);}
    }
    void Paint(){
        if(!active||image.pixels.empty())return;Resources();if(mode!=Mode::MovingObject)CacheAnnotations();UploadLiveStroke();PreparePalette();
        const bool rebuild=sceneDirty||!sceneBitmap;PaintScene();const RECT currentOverlay=OverlayBounds();RECT dirty{};
        if(rebuild)dirty={0,0,image.width,image.height};else UnionRect(&dirty,&previousOverlay,&currentOverlay);
        previousOverlay=currentOverlay;if(IsRectEmpty(&dirty)){if(retryPresent)PresentFrame(S_OK);return;}
        target->BeginDraw();target->SetTransform(D2D1::Matrix3x2F::Identity());
        // Восстанавливаем прежний указатель и рисуем новый только в затронутой части сохранённого кадра.
        target->PushAxisAlignedClip(D2D1::RectF(static_cast<float>(dirty.left),static_cast<float>(dirty.top),static_cast<float>(dirty.right),static_cast<float>(dirty.bottom)),D2D1_ANTIALIAS_MODE_ALIASED);
        target->DrawBitmap(sceneBitmap.Get());PaintPicker();PaintBrushPreview();if(PickerAtMouse())PaintToolPointer(ui::Icon::Eyedropper);
        target->PopAxisAlignedClip();PresentFrame(target->EndDraw());
    }
    void PaintFrame(){framePending=false;if(settings)PaintSettings();else if(trayMenu)PaintTray();else Paint();}
    Image Export(){
        CommitText();const int x=std::clamp(static_cast<int>(selection.left),0,image.width-1),y=std::clamp(static_cast<int>(selection.top),0,image.height-1);
        const int width=std::clamp(static_cast<int>(selection.right)-x,1,image.width-x),height=std::clamp(static_cast<int>(selection.bottom)-y,1,image.height-y);
        Image result{x,y,width,height,{}};const HDC dc=CreateCompatibleDC(nullptr);
        BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
        void* bits{};const HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&bits,nullptr,0);
        if(!bitmap){DeleteDC(dc);throw std::runtime_error("Не удалось подготовить изображение");}
        const auto old=SelectObject(dc,bitmap);ComPtr<ID2D1DCRenderTarget> canvas;
        const auto properties=D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_IGNORE),96,96);
        HRESULT hr=factory->CreateDCRenderTarget(&properties,&canvas);
        RECT r{0,0,width,height};if(SUCCEEDED(hr))hr=canvas->BindDC(dc,&r);
        if(SUCCEEDED(hr)){
            ComPtr<ID2D1Bitmap> bg;ComPtr<ID2D1SolidColorBrush> paint;
            hr=canvas->CreateBitmap(D2D1::SizeU(image.width,image.height),image.pixels.data(),image.width*4,D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_IGNORE)),&bg);
            canvas->CreateSolidColorBrush(D2D1::ColorF(color),&paint);
            if(SUCCEEDED(hr)&&paint){canvas->BeginDraw();canvas->SetTransform(D2D1::Matrix3x2F::Translation(static_cast<float>(-x),static_cast<float>(-y)));canvas->DrawBitmap(bg.Get());
                for(const auto& a:annotations)DrawAnnotation(canvas.Get(),paint.Get(),*a,bg.Get());hr=canvas->EndDraw();
                if(SUCCEEDED(hr)){auto* pixels=static_cast<std::uint32_t*>(bits);result.pixels.assign(pixels,pixels+static_cast<size_t>(width)*height);for(auto& pixel:result.pixels)pixel|=0xff000000;}
            }
        }
        SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);
        if(FAILED(hr)||result.pixels.empty())throw std::runtime_error("Не удалось отрисовать скриншот");return result;
    }
    void Copy(){if(selected){CopyImage(Export(),window);FinishCapture();}}
    void Save();
    ComPtr<IDWriteTextLayout> CreateTextLayout(const Annotation& annotation){
        ComPtr<IDWriteTextFormat> format;ComPtr<IDWriteTextLayout> layout;
        if(FAILED(textFactory->CreateTextFormat(L"Segoe UI",nullptr,DWRITE_FONT_WEIGHT_SEMI_BOLD,DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,annotation.fontSize,L"ru-ru",&format)))return {};
        format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        if(FAILED(textFactory->CreateTextLayout(annotation.text.c_str(),static_cast<UINT32>(annotation.text.size()),format.Get(),16384,16384,&layout)))return {};return layout;
    }
    void ResetTextCaret(){caretVisible=true;const UINT blink=GetCaretBlinkTime();if(blink==INFINITE)KillTimer(window,1);else SetTimer(window,1,std::max(100u,blink),nullptr);RequestFrame();}
    void ReadText(WPARAM navigation=0){
        if(!edit)return;const int length=GetWindowTextLengthW(edit);std::wstring content(static_cast<size_t>(length)+1,0);GetWindowTextW(edit,content.data(),length+1);content.resize(length);
        if(content!=preview.text||!textLayout){preview.text=std::move(content);textLayout=CreateTextLayout(preview);}
        DWORD start{},end{};SendMessageW(edit,EM_GETSEL,reinterpret_cast<WPARAM>(&start),reinterpret_cast<LPARAM>(&end));
        textCaret=std::min<UINT32>(navigation==VK_LEFT||navigation==VK_HOME||navigation==VK_UP?start:end,static_cast<UINT32>(preview.text.size()));textAnchor=textCaret==start?end:start;ResetTextCaret();
    }
    void UpdateTextFont(){
        const HFONT next=CreateFontW(-static_cast<int>(preview.fontSize),0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        if(next){SendMessageW(edit,WM_SETFONT,reinterpret_cast<WPARAM>(next),FALSE);if(textInputFont)DeleteObject(textInputFont);textInputFont=next;}
        textLayout=CreateTextLayout(preview);ResetTextCaret();
    }
    D2D1_RECT_F TextBounds()const{
        const auto p=preview.points.front();DWRITE_TEXT_METRICS metrics{};if(textLayout)textLayout->GetMetrics(&metrics);
        return D2D1::RectF(p.x-3,p.y-3,p.x+std::max(40.0f,metrics.widthIncludingTrailingWhitespace)+6,p.y+std::max(preview.fontSize*1.3f,metrics.height)+3);
    }
    void PaintTextEditor(){
        if(!textLayout)return;const auto p=preview.points.front();const auto bounds=TextBounds();SetColor(brush.Get(),0xbca3f3,.8f);target->DrawRectangle(bounds,brush.Get(),1);
        DWORD start{},end{};SendMessageW(edit,EM_GETSEL,reinterpret_cast<WPARAM>(&start),reinterpret_cast<LPARAM>(&end));
        if(end>start){UINT32 count{};textLayout->HitTestTextRange(start,end-start,p.x,p.y,nullptr,0,&count);if(count){std::vector<DWRITE_HIT_TEST_METRICS> hits(count);if(SUCCEEDED(textLayout->HitTestTextRange(start,end-start,p.x,p.y,hits.data(),count,&count))){SetColor(brush.Get(),0x836bd2,.4f);for(const auto& hit:hits)target->FillRectangle(D2D1::RectF(hit.left,hit.top,hit.left+hit.width,hit.top+hit.height),brush.Get());}}}
        const auto previous=target->GetTextAntialiasMode();target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);SetColor(brush.Get(),preview.color);target->DrawTextLayout(p,textLayout.Get(),brush.Get());target->SetTextAntialiasMode(previous);
        if(caretVisible&&GetFocus()==edit){float x{},y{};DWRITE_HIT_TEST_METRICS hit{};if(SUCCEEDED(textLayout->HitTestTextPosition(textCaret,FALSE,&x,&y,&hit))){SetColor(brush.Get(),0xffffff);target->DrawLine(D2D1::Point2F(p.x+x,p.y+y),D2D1::Point2F(p.x+x,p.y+y+std::max(hit.height,preview.fontSize)),brush.Get(),1);}}
    }
    UINT32 TextPosition(D2D1_POINT_2F point){
        BOOL trailing{},inside{};DWRITE_HIT_TEST_METRICS hit{};const auto p=preview.points.front();if(!textLayout||FAILED(textLayout->HitTestPoint(point.x-p.x,point.y-p.y,&trailing,&inside,&hit)))return 0;
        return std::min(hit.textPosition+(trailing?hit.length:0),static_cast<UINT32>(preview.text.size()));
    }
    void SelectTextAt(D2D1_POINT_2F point,bool extend){
        if(!extend)textAnchor=TextPosition(point);textCaret=TextPosition(point);SendMessageW(edit,EM_SETSEL,textAnchor,textCaret);SetFocus(edit);ResetTextCaret();
    }
    void StartText(D2D1_POINT_2F point){
        editingText=HitObject(point);if(editingText>=0&&annotations[editingText]->tool==Tool::Text){preview=*annotations[editingText];textSize=preview.fontSize;annotationsDirty=true;}
        else{editingText=-1;preview={Tool::Text,{point},color,thickness,{}};preview.fontSize=textSize;}
        // EDIT обеспечивает нативный ввод и буфер текста; видимую часть рисуем тем же DirectWrite, что и PNG.
        edit=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_VISIBLE|ES_MULTILINE|ES_AUTOVSCROLL|ES_AUTOHSCROLL,-16,-16,1,1,window,nullptr,instance,nullptr);
        if(!edit){editingText=-1;annotationsDirty=true;throw std::runtime_error("Не удалось начать ввод текста");}
        SetWindowSubclass(edit,EditProc,1,reinterpret_cast<DWORD_PTR>(this));SendMessageW(edit,EM_SETLIMITTEXT,32767,0);SetWindowTextW(edit,preview.text.c_str());SendMessageW(edit,EM_SETSEL,preview.text.size(),preview.text.size());UpdateTextFont();SetFocus(edit);HideCaret(edit);ReadText();RefreshCursor();
    }
    void CancelText(){
        if(!edit)return;const HWND previous=edit;edit=nullptr;DestroyWindow(previous);KillTimer(window,1);textLayout.Reset();if(textSelecting||textDragging)ReleaseCapture();textSelecting=false;textDragging=false;editingText=-1;annotationsDirty=true;RequestFrame();
    }
    void CommitText(){
        if(!edit)return;ReadText();const int replace=editingText;CancelText();SetFocus(window);
        if(replace>=0){const auto& original=*annotations[replace];if(original.text!=preview.text||original.fontSize!=preview.fontSize||original.color!=preview.color||Distance(original.points.front(),preview.points.front())>.1f){Remember();if(preview.text.empty())annotations.erase(annotations.begin()+replace);else annotations[replace]=std::make_shared<const Annotation>(preview);}}
        else if(!preview.text.empty())Append(preview);RefreshCursor(true);RequestFrame();
    }
    bool TextHasSelection()const{
        DWORD start{},end{};SendMessageW(edit,EM_GETSEL,reinterpret_cast<WPARAM>(&start),reinterpret_cast<LPARAM>(&end));return end>start;
    }
    static LRESULT CALLBACK EditProc(HWND hwnd,UINT message,WPARAM wparam,LPARAM lparam,UINT_PTR,DWORD_PTR data){
        auto* self=reinterpret_cast<Impl*>(data);
        if((message==WM_SYSKEYDOWN||message==WM_SYSKEYUP)&&wparam==VK_MENU){self->Modifier(wparam,message==WM_SYSKEYDOWN);return 0;}
        if((message==WM_COPY||(message==WM_KEYDOWN&&wparam=='C'&&(GetKeyState(VK_CONTROL)&0x8000)))&&!self->TextHasSelection()){
            if(GetMessageExtraInfo()!=0xca7e)self->Copy();return 0;
        }
        if(message==WM_KEYDOWN&&wparam=='S'&&(GetKeyState(VK_CONTROL)&0x8000)){self->Save();return 0;}
        if((message==WM_KEYDOWN||message==WM_KEYUP)&&wparam==VK_TAB){self->Modifier(wparam,message==WM_KEYDOWN);return 0;}
        if(message==WM_CHAR&&wparam==VK_TAB)return 0;
        if(message==WM_KEYDOWN&&wparam=='A'&&(GetKeyState(VK_CONTROL)&0x8000)){SendMessageW(hwnd,EM_SETSEL,0,-1);self->ReadText();return 0;}
        if(message==WM_CHAR&&wparam==1)return 0;
        if(message==WM_KEYDOWN&&wparam==VK_RETURN&&!(GetKeyState(VK_SHIFT)&0x8000)){self->CommitText();return 0;}
        if(message==WM_KEYDOWN&&wparam==VK_ESCAPE){self->CancelText();SetFocus(self->window);self->RefreshCursor(true);return 0;}
        if(message==WM_MOUSEWHEEL){self->Wheel(GET_WHEEL_DELTA_WPARAM(wparam),GET_KEYSTATE_WPARAM(wparam));self->RefreshCursor();return 0;}
        if(message==WM_KEYDOWN&&wparam==VK_F2){self->CommitText();self->ChooseFolder();return 0;}
        const auto result=DefSubclassProc(hwnd,message,wparam,lparam);
        if(self->edit==hwnd&&(message==WM_CHAR||message==WM_KEYDOWN||message==WM_PASTE||message==WM_CUT||message==WM_CLEAR||message==WM_UNDO||message==WM_IME_COMPOSITION)){self->ReadText(message==WM_KEYDOWN?wparam:0);}
        return result;
    }
    bool HitAnnotation(const Annotation& a,D2D1_POINT_2F point){
        if(a.points.empty())return false;const auto p=a.points.front(),q=a.points.back();
        const auto segment=[&](D2D1_POINT_2F start,D2D1_POINT_2F end){const float dx=end.x-start.x,dy=end.y-start.y,length=dx*dx+dy*dy;const float t=length>.0001f?std::clamp(((point.x-start.x)*dx+(point.y-start.y)*dy)/length,0.0f,1.0f):0;return Distance(point,D2D1::Point2F(start.x+t*dx,start.y+t*dy));};
        if(IsBrush(a.tool)){if(!a.stroke)return false;const auto& stroke=*a.stroke;const int x=static_cast<int>(std::floor(point.x-p.x-stroke.offsetX)),y=static_cast<int>(std::floor(point.y-p.y-stroke.offsetY));return x>=0&&y>=0&&x<stroke.width&&y<stroke.height&&(stroke.pixels[static_cast<size_t>(y)*stroke.width+x]>>24)!=0;}
        if(a.tool==Tool::Fill&&a.fill){const int x=static_cast<int>(std::floor(point.x-p.x)),y=static_cast<int>(std::floor(point.y-p.y));return x>=0&&y>=0&&x<a.fill->width&&y<a.fill->height&&(a.fill->pixels[static_cast<size_t>(y)*a.fill->width+x]>>24)!=0;}
        const float tolerance=a.width/2+3;const auto r=Normalize(p,q);
        if(a.tool==Tool::Line)return segment(p,q)<=tolerance;
        if(a.tool==Tool::Arrow){if(segment(p,q)<=tolerance)return true;const float length=Distance(p,q);if(length<.01f)return false;const float dx=(q.x-p.x)/length,dy=(q.y-p.y)/length,head=std::min(std::max(12.0f,a.width*4),length*.8f),half=head*.4f;const auto b=D2D1::Point2F(q.x-dx*head-dy*half,q.y-dy*head+dx*half),c=D2D1::Point2F(q.x-dx*head+dy*half,q.y-dy*head-dx*half);const auto cross=[&](D2D1_POINT_2F u,D2D1_POINT_2F v){return (point.x-v.x)*(u.y-v.y)-(u.x-v.x)*(point.y-v.y);};const float x=cross(q,b),y=cross(b,c),z=cross(c,q);return(x>=0&&y>=0&&z>=0)||(x<=0&&y<=0&&z<=0);}
        if(a.tool==Tool::Rectangle)return segment(D2D1::Point2F(r.left,r.top),D2D1::Point2F(r.right,r.top))<=tolerance||segment(D2D1::Point2F(r.right,r.top),D2D1::Point2F(r.right,r.bottom))<=tolerance||segment(D2D1::Point2F(r.right,r.bottom),D2D1::Point2F(r.left,r.bottom))<=tolerance||segment(D2D1::Point2F(r.left,r.bottom),D2D1::Point2F(r.left,r.top))<=tolerance;
        if(a.tool==Tool::Ellipse){const float rx=(r.right-r.left)/2,ry=(r.bottom-r.top)/2;if(rx<.01f||ry<.01f)return false;const float nx=(point.x-(r.left+r.right)/2)/rx,ny=(point.y-(r.top+r.bottom)/2)/ry;return std::abs(std::hypot(nx,ny)-1)*std::min(rx,ry)<=tolerance;}
        if(a.tool==Tool::Number)return Distance(p,point)<=18;
        if(a.tool==Tool::Text){const auto layout=CreateTextLayout(a);if(!layout)return false;DWRITE_TEXT_METRICS metrics{};layout->GetMetrics(&metrics);return Inside(D2D1::RectF(p.x,p.y,p.x+metrics.widthIncludingTrailingWhitespace,p.y+metrics.height),point);}
        return Inside(r,point);
    }
    int HitObject(D2D1_POINT_2F point){
        for(int i=static_cast<int>(annotations.size())-1;i>=0;--i){
            const auto& a=*annotations[i];if(HitAnnotation(a,point))return a.tool==Tool::Eraser?-1:i;
        }return -1;
    }
    void FillAt(D2D1_POINT_2F point){const auto composite=Export();auto annotation=FillConnectedRegion(composite,static_cast<int>(point.x)-composite.x,static_cast<int>(point.y)-composite.y,color);if(annotation.fill)Append(std::move(annotation));RequestFrame();}
    void Down(D2D1_POINT_2F point){
        if(palette){PaletteDown(point);return;}
        if(edit&&!OverControls()&&Inside(TextBounds(),point)){
            mouse=point;
            if(picker){textDragging=true;textDragStart=point;textOrigin=preview.points.front();}
            else{SelectTextAt(point,(GetKeyState(VK_SHIFT)&0x8000)!=0);textSelecting=true;}
            if(!headless)SetCapture(window);RefreshCursor();return;
        }
        CommitText();mouse=point;snapShapes=(GetKeyState(VK_SHIFT)&0x8000)!=0;
        if(Inside(D2D1::RectF(static_cast<float>(image.width-44),12,static_cast<float>(image.width-12),44),point)){if(demo)DestroyWindow(window);else FinishCapture();return;}
        if(selected&&!dragging){
            const auto panel=Tools(),actions=Actions();
            if(Inside(panel,point)){
                bool handled=false;
                for(int i=0;i<ToolCount;++i)if(Inside(ToolButton(i),point)){SelectTool(static_cast<Tool>(i));handled=true;break;}
                if(!handled){if(Inside(UndoButton(false),point))Undo();else if(Inside(UndoButton(true),point))Redo();else if(Inside(ColorButton(),point))OpenPalette(D2D1::Point2F(panel.right+10,panel.top));}
                RequestFrame();return;
            }
            if(Inside(actions,point)){const float x=point.x-actions.left;if(x<140)Copy();else if(x<278)Save();else if(x<380)ChooseFolder();else FinishCapture();return;}
        }
        if(PickerAtMouse()){
            const int x=std::clamp(static_cast<int>(point.x),0,image.width-1),y=std::clamp(static_cast<int>(point.y),0,image.height-1);
            color=image.pixels[static_cast<size_t>(y)*image.width+x]&0xffffff;RequestFrame();return;
        }
        down=point;originalSelection=selection;selectedBefore=selected;handle=-1;
        if(selected){const auto handles=Handles();for(int i=0;i<8;++i)if(Distance(point,handles[i])<10)handle=i;}
        if(handle>=0)mode=Mode::Resizing;
        else if(!selected||!Inside(selection,point)){mode=Mode::Creating;selected=true;selection=Normalize(point,point);}
        else if(tool==Tool::Select){object=HitObject(point);if(object>=0){mode=Mode::MovingObject;originalObject=*annotations[object];preview=originalObject;}else mode=Mode::Moving;}
        else if(tool==Tool::Eraser&&(GetKeyState(VK_SHIFT)&0x8000)){const int hit=HitObject(point);if(hit>=0){Remember();annotations.erase(annotations.begin()+hit);}RequestFrame();return;}
        else if(tool==Tool::Fill){FillAt(point);return;}
        else if(tool==Tool::Text){
            StartText(point);RequestFrame();return;
        }else if(tool==Tool::Number){Append({tool,{point},color,thickness,std::to_wstring(NextNumber())});RequestFrame();return;}
        else{mode=Mode::Drawing;preview={tool,{point},color,thickness,{}};preview.pixelSize=pixelSize;if(IsBrush(tool)){preview.widths={thickness};liveStroke.Begin(image.width,image.height,point,BrushRadius(thickness));liveStrokeBitmap.Reset();}else preview.points.push_back(point);}
        dragging=true;if(!headless)SetCapture(window);RequestFrame();
    }
    void Move(D2D1_POINT_2F point){
        if(textDragging){mouse=point;preview.points.front()=D2D1::Point2F(std::clamp(textOrigin.x+point.x-textDragStart.x,0.0f,static_cast<float>(image.width)),std::clamp(textOrigin.y+point.y-textDragStart.y,0.0f,static_cast<float>(image.height)));RequestFrame();return;}
        if(textSelecting){mouse=point;SelectTextAt(point,true);return;}
        if(paletteDrag){mouse=point;PaletteValue(point);return;}
        point.x=std::clamp(point.x,0.0f,static_cast<float>(image.width));point.y=std::clamp(point.y,0.0f,static_cast<float>(image.height));mouse=point;
        if(dragging){
            const float dx=point.x-down.x,dy=point.y-down.y;
            if(mode==Mode::Creating)selection=Normalize(down,point);
            else if(mode==Mode::Moving){
                const float x=std::clamp(originalSelection.left+dx,0.0f,image.width-(originalSelection.right-originalSelection.left)),y=std::clamp(originalSelection.top+dy,0.0f,image.height-(originalSelection.bottom-originalSelection.top));
                selection=D2D1::RectF(x,y,x+originalSelection.right-originalSelection.left,y+originalSelection.bottom-originalSelection.top);
            }else if(mode==Mode::Resizing){
                auto r=originalSelection;if(handle==0||handle==6||handle==7)r.left=point.x;if(handle==2||handle==3||handle==4)r.right=point.x;if(handle==0||handle==1||handle==2)r.top=point.y;if(handle==4||handle==5||handle==6)r.bottom=point.y;
                selection=Normalize(D2D1::Point2F(r.left,r.top),D2D1::Point2F(r.right,r.bottom));
            }else if(mode==Mode::Drawing){
                if(IsBrush(preview.tool)){if(Distance(preview.points.back(),point)>=.7f){preview.points.push_back(point);preview.widths.push_back(thickness);liveStroke.Add(point,BrushRadius(thickness));}}else preview.points.back()=ConstrainEndpoint(down,point,preview.tool);
            }else if(mode==Mode::MovingObject&&object>=0){preview=originalObject;for(auto& p:preview.points){p.x+=dx;p.y+=dy;}}
        }
        const int nextHover=HoverAt(point);const bool changed=nextHover!=hover;hover=nextHover;
        if(dragging||picker||tool==Tool::Eyedropper||changed||selected||palette)RequestFrame(dragging||changed||palette||edit);
    }
    void Up(D2D1_POINT_2F point){
        if(textDragging){Move(point);textDragging=false;ReleaseCapture();SetFocus(edit);RefreshCursor();return;}
        if(textSelecting){SelectTextAt(point,true);textSelecting=false;ReleaseCapture();return;}
        if(paletteDrag){mouse=point;PaletteValue(point);paletteDrag=0;ReleaseCapture();return;}
        if(!dragging)return;Move(point);if(mode==Mode::Drawing){if(IsBrush(preview.tool))preview.stroke=liveStroke.Finish();Append(preview);}
        else if(mode==Mode::MovingObject&&object>=0&&Distance(point,down)>.1f){Remember();annotations[object]=std::make_shared<const Annotation>(preview);}
        liveStroke.Reset();liveStrokeBitmap.Reset();dragging=false;mode=Mode::None;ReleaseCapture();if(selection.right-selection.left<2||selection.bottom-selection.top<2)selected=false;RequestFrame();
    }
    void Key(WPARAM key){
        const bool ctrl=(GetKeyState(VK_CONTROL)&0x8000)!=0,shift=(GetKeyState(VK_SHIFT)&0x8000)!=0;
        if(settings||trayMenu){if(key==VK_ESCAPE)Hide();else if(settings&&key==VK_F2)ChooseFolder();return;}
        if(palette){if(key==VK_ESCAPE||key==VK_RETURN)ClosePalette();return;}
        // Caramba читает выделенный текст через Ctrl+C с меткой 0xCA7E.
        // На холсте это запрос переключателя, а не команда копирования снимка.
        if(!edit&&ctrl&&key=='C'&&GetMessageExtraInfo()==0xca7e)return;
        if(key==VK_ESCAPE){if(dragging){liveStroke.Reset();liveStrokeBitmap.Reset();dragging=false;mode=Mode::None;ReleaseCapture();selection=originalSelection;selected=selectedBefore;RequestFrame();}else FinishCapture();return;}
        if(ctrl&&((key=='Z'&&shift)||key=='Y'))Redo();else if(ctrl&&key=='Z')Undo();else if(ctrl&&key=='C')Copy();else if(ctrl&&key=='S')Save();else if(key==VK_F2)ChooseFolder();else if(key==VK_RETURN)Copy();
        else if(!dragging){if(key=='P'&&!ctrl)SelectTool(Tool::Pen);else if(key=='A'&&!ctrl)SelectTool(Tool::Arrow);else if(key=='T'&&!ctrl)SelectTool(Tool::Text);else if(key>='1'&&key<='9'&&!ctrl)SelectTool(static_cast<Tool>(key-'1'));}
        RequestFrame();
    }
    void Wheel(int delta,UINT modifiers){
        if(settings||trayMenu)return;
        RefreshCursor(true);
        wheelRemainder+=delta;const int steps=wheelRemainder/WHEEL_DELTA;wheelRemainder%=WHEEL_DELTA;if(!steps)return;
        if(modifiers&MK_SHIFT){if(dragging||paletteDrag||textSelecting||textDragging)return;CommitText();if(palette)ClosePalette();const int next=(static_cast<int>(tool)-steps%ToolCount+ToolCount)%ToolCount;SelectTool(static_cast<Tool>(next));RefreshCursor(true);RequestFrame();return;}
        if(palette){if(Inside(ColorField(),mouse)||OnBrightness(mouse)){value=std::clamp(value+steps*.04f,0.0f,1.0f);color=ui::HsvToRgb(hue,saturation,value);SyncHex();RequestFrame();}return;}
        if(edit||tool==Tool::Text){textSize=std::clamp(textSize+steps*2,8.0f,192.0f);if(edit){preview.fontSize=textSize;UpdateTextFont();}RequestFrame();return;}
        if(tool==Tool::Pixelate){pixelSize=std::clamp(pixelSize+steps*2,2,96);if(mode==Mode::Drawing&&preview.tool==Tool::Pixelate)preview.pixelSize=pixelSize;}
        else{const float next=std::clamp(thickness+steps,1.0f,128.0f);if(next==thickness)return;thickness=next;if(mode==Mode::Drawing){if(IsBrush(preview.tool)){preview.points.push_back(mouse);preview.widths.push_back(thickness);liveStroke.Add(mouse,BrushRadius(thickness));}else preview.width=thickness;}}
        RequestFrame();
    }
    D2D1_POINT_2F ConstrainEndpoint(D2D1_POINT_2F origin,D2D1_POINT_2F point,Tool shape)const{
        if(!snapShapes)return point;
        float dx=point.x-origin.x,dy=point.y-origin.y;
        if(shape==Tool::Rectangle||shape==Tool::Ellipse){
            const float sx=dx<0?-1.0f:1.0f,sy=dy<0?-1.0f:1.0f;
            const float size=std::min({std::max(std::abs(dx),std::abs(dy)),sx<0?origin.x:image.width-origin.x,sy<0?origin.y:image.height-origin.y});
            return D2D1::Point2F(origin.x+sx*size,origin.y+sy*size);
        }
        if(shape==Tool::Line||shape==Tool::Arrow){
            constexpr float step=3.14159265358979323846f/4;const float angle=std::round(std::atan2(dy,dx)/step)*step,length=std::hypot(dx,dy);
            dx=std::cos(angle)*length;dy=std::sin(angle)*length;float scale=1;
            if(std::abs(dx)>.0001f)scale=std::min(scale,(dx<0?origin.x:image.width-origin.x)/std::abs(dx));
            if(std::abs(dy)>.0001f)scale=std::min(scale,(dy<0?origin.y:image.height-origin.y)/std::abs(dy));
            return D2D1::Point2F(origin.x+dx*scale,origin.y+dy*scale);
        }
        return point;
    }
    D2D1_RECT_F CloseButton()const{return D2D1::RectF(static_cast<float>(image.width-44),12,static_cast<float>(image.width-12),44);}
    int HandleAtMouse()const{if(!ToolsVisible()||dragging)return -1;const auto handles=Handles();for(int i=0;i<8;++i)if(Distance(mouse,handles[i])<10)return i;return -1;}
    bool OverControls()const{return Inside(CloseButton(),mouse)||(ToolsVisible()&&(Inside(Tools(),mouse)||Inside(Actions(),mouse)));}
    bool CanvasAtMouse()const{return active&&!trayMenu&&!settings&&mouseInside&&!palette&&!edit&&mode!=Mode::Creating&&(mode==Mode::Drawing||(!OverControls()&&HandleAtMouse()<0));}
    bool PickerAtMouse()const{return CanvasAtMouse()&&!dragging&&(picker||tool==Tool::Eyedropper);}
    bool BrushAtMouse()const{return CanvasAtMouse()&&selected&&!PickerAtMouse()&&(mode==Mode::None||mode==Mode::Drawing)&&(mode==Mode::Drawing||Inside(selection,mouse))&&(tool==Tool::Pen||tool==Tool::Eraser||tool==Tool::Pixelate||tool==Tool::Line||tool==Tool::Arrow||tool==Tool::Rectangle||tool==Tool::Ellipse);}
    bool ToolPointerAtMouse()const{return BrushAtMouse()||PickerAtMouse()||(CanvasAtMouse()&&selected&&Inside(selection,mouse)&&(tool==Tool::Fill||tool==Tool::Number||tool==Tool::Eraser));}
    bool InteractiveAtMouse()const{
        if(settings)return Inside(CloseButton(),mouse)||Inside(StartupButton(),mouse)||(!startupCommand.empty()&&!startupHere&&Inside(StartupOffButton(),mouse))||Inside(FolderSettingButton(),mouse)||Inside(HotkeySettingButton(),mouse)||Inside(HotkeyValue(),mouse);
        if(trayMenu){for(int i=0;i<4;++i)if(Inside(TrayRow(i),mouse))return true;return false;}
        if(palette){if(Inside(PaletteClose(),mouse)||Inside(ColorField(),mouse)||OnBrightness(mouse))return true;for(int i=0;i<24;++i)if(Inside(ColorCell(i),mouse))return true;for(int i=0;i<12;++i)if(Inside(ColorCell(i,true),mouse)||(customColors[i]&&Inside(ColorDelete(i),mouse)))return true;return false;}
        if(dragging)return false;
        if(mode!=Mode::Creating&&Inside(CloseButton(),mouse))return true;
        if(!ToolsVisible()||dragging)return false;
        for(int i=0;i<ToolCount;++i)if(Inside(ToolButton(i),mouse))return true;
        if(Inside(ColorButton(),mouse)||(!undoStack.empty()&&Inside(UndoButton(false),mouse))||(!redoStack.empty()&&Inside(UndoButton(true),mouse)))return true;
        if(Inside(Actions(),mouse)){const float x=mouse.x-Actions().left;return (x>=5&&x<=140)||(x>=142&&x<=278)||(x>=280&&x<=380)||(x>=385&&x<=419);}
        return false;
    }
    HCURSOR CursorAtMouse(){
        if(edit&&(textDragging||(picker&&!OverControls()&&Inside(TextBounds(),mouse))))return LoadCursorW(nullptr,IDC_SIZEALL);
        if(edit&&(textSelecting||(!OverControls()&&Inside(TextBounds(),mouse))))return LoadCursorW(nullptr,IDC_IBEAM);
        if(hexEdit&&Inside(D2D1::RectF(paletteBounds.left+31,paletteBounds.top+283,paletteBounds.left+107,paletteBounds.top+305),mouse))return LoadCursorW(nullptr,IDC_IBEAM);
        if(InteractiveAtMouse())return cursors.Hand();
        if(palette||trayMenu||settings)return cursors.Arrow();
        if(dragging&&(mode==Mode::Moving||mode==Mode::MovingObject))return LoadCursorW(nullptr,IDC_SIZEALL);
        const int grip=mode==Mode::Resizing?handle:HandleAtMouse();
        if(grip>=0)return LoadCursorW(nullptr,grip==0||grip==4?IDC_SIZENWSE:grip==2||grip==6?IDC_SIZENESW:grip==1||grip==5?IDC_SIZENS:IDC_SIZEWE);
        // Отрисованный указатель инструмента заменяет системную стрелку на холсте.
        if(ToolPointerAtMouse())return nullptr;
        if(CanvasAtMouse()&&selected&&Inside(selection,mouse)){if(tool==Tool::Text)return LoadCursorW(nullptr,IDC_IBEAM);if(tool==Tool::Select)return LoadCursorW(nullptr,IDC_SIZEALL);}
        return cursors.Arrow();
    }
    void RefreshCursor(bool readPosition=false){
        if(!active||headless)return;
        if(readPosition){POINT position{};GetCursorPos(&position);ScreenToClient(window,&position);mouse=D2D1::Point2F(static_cast<float>(position.x),static_cast<float>(position.y));RECT bounds{};GetClientRect(window,&bounds);mouseInside=PtInRect(&bounds,position)!=FALSE;if(mouseInside){TRACKMOUSEEVENT tracking{sizeof(tracking),TME_LEAVE,window,0};if(!headless)TrackMouseEvent(&tracking);}}
        if(mouseInside||dragging)SetCursor(CursorAtMouse());
    }
    void Modifier(WPARAM key,bool pressed){
        if(key==VK_MENU||key==VK_LMENU||key==VK_RMENU){if(picker==pressed)return;picker=pressed;}
        else if(key==VK_SHIFT||key==VK_LSHIFT||key==VK_RSHIFT){if(snapShapes==pressed)return;snapShapes=pressed;if(mode==Mode::Drawing&&!IsBrush(preview.tool))Move(mouse);}
        else if(key==VK_TAB){if(guides==pressed)return;guides=pressed;}
        else return;
        // Клавиша сама обновляет указатель: ждать нового WM_SETCURSOR от мыши нельзя.
        RefreshCursor(!dragging);RequestFrame();
    }
    void PaintToolPointer(ui::Icon icon){
        if(icon==ui::Icon::Eyedropper){
            // Центр указателя совпадает с центром именно того пикселя, который читает пипетка.
            const float x=static_cast<float>(std::clamp(static_cast<int>(mouse.x),0,image.width-1))+.5f,y=static_cast<float>(std::clamp(static_cast<int>(mouse.y),0,image.height-1))+.5f;
            const auto antialias=target->GetAntialiasMode();target->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
            for(int i=0;i<2;++i){SetColor(brush.Get(),i?0xffffff:0x06090f);const float width=i?1.0f:3.0f;
                target->DrawLine(D2D1::Point2F(x-9,y),D2D1::Point2F(x-4,y),brush.Get(),width);target->DrawLine(D2D1::Point2F(x+4,y),D2D1::Point2F(x+9,y),brush.Get(),width);
                target->DrawLine(D2D1::Point2F(x,y-9),D2D1::Point2F(x,y-4),brush.Get(),width);target->DrawLine(D2D1::Point2F(x,y+4),D2D1::Point2F(x,y+9),brush.Get(),width);
            }
            SetColor(brush.Get(),0x06090f);target->FillRectangle(D2D1::RectF(x-1.5f,y-1.5f,x+1.5f,y+1.5f),brush.Get());
            SetColor(brush.Get(),0xffffff);target->FillRectangle(D2D1::RectF(x-.5f,y-.5f,x+.5f,y+.5f),brush.Get());target->SetAntialiasMode(antialias);
        }
        // У ластика сохраняется круг; Shift добавляет точку в центре и корзину.
        if(icon==ui::Icon::Eraser&&snapShapes){SetColor(brush.Get(),0x06090f);target->FillEllipse(D2D1::Ellipse(mouse,2.6f,2.6f),brush.Get());SetColor(brush.Get(),0xffffff);target->FillEllipse(D2D1::Ellipse(mouse,1.1f,1.1f),brush.Get());}
        const auto r=D2D1::RectF(mouse.x+12,mouse.y-21,mouse.x+28,mouse.y-5);
        Icon(icon,D2D1::RectF(r.left+1,r.top+1,r.right+1,r.bottom+1),0x080d16);Icon(icon,r,0xf3edff);
        if(icon==ui::Icon::Eraser&&snapShapes){const auto trash=D2D1::RectF(r.right-4,r.bottom-8,r.right+8,r.bottom+4);Icon(ui::Icon::Trash,D2D1::RectF(trash.left+1,trash.top+1,trash.right+1,trash.bottom+1),0x080d16);Icon(ui::Icon::Trash,trash,0xffa8b6);}
    }
    int NextNumber()const{int number=1;for(const auto& a:annotations)if(a->tool==Tool::Number)++number;return number;}
    void PaintPlacementReticle(float inner,float outer){
        const auto previous=target->GetAntialiasMode();target->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
        for(int pass=0;pass<2;++pass){SetColor(brush.Get(),pass?0xffffff:0x06090f);const float width=pass?1.0f:3.0f;
            target->DrawLine(D2D1::Point2F(mouse.x-outer,mouse.y),D2D1::Point2F(mouse.x-inner,mouse.y),brush.Get(),width);
            target->DrawLine(D2D1::Point2F(mouse.x+inner,mouse.y),D2D1::Point2F(mouse.x+outer,mouse.y),brush.Get(),width);
            target->DrawLine(D2D1::Point2F(mouse.x,mouse.y-outer),D2D1::Point2F(mouse.x,mouse.y-inner),brush.Get(),width);
            target->DrawLine(D2D1::Point2F(mouse.x,mouse.y+inner),D2D1::Point2F(mouse.x,mouse.y+outer),brush.Get(),width);}
        SetColor(brush.Get(),0x06090f);target->FillEllipse(D2D1::Ellipse(mouse,2.5f,2.5f),brush.Get());
        SetColor(brush.Get(),0xffffff);target->FillEllipse(D2D1::Ellipse(mouse,1.0f,1.0f),brush.Get());target->SetAntialiasMode(previous);
    }
    void PaintBrushPreview(){
        if(!BrushAtMouse()){
            if(ToolPointerAtMouse()&&!PickerAtMouse()){
                if(tool==Tool::Number){Annotation stamp{Tool::Number,{mouse},color,thickness,std::to_wstring(NextNumber())};DrawAnnotation(target.Get(),brush.Get(),stamp,background.Get());PaintPlacementReticle(18,24);}
                else if(tool==Tool::Fill)PaintPlacementReticle(4,11);
                PaintToolPointer(static_cast<ui::Icon>(tool));
            }return;
        }
        const float size=tool==Tool::Pixelate?static_cast<float>(pixelSize):thickness,radius=std::max(.5f,size/2);
        for(int i=0;i<2;++i){SetColor(brush.Get(),i?0xffffff:0x06090f);const float width=i?1.0f:3.0f;if(tool==Tool::Pixelate)target->DrawRectangle(D2D1::RectF(mouse.x-radius,mouse.y-radius,mouse.x+radius,mouse.y+radius),brush.Get(),width);else target->DrawEllipse(D2D1::Ellipse(mouse,radius,radius),brush.Get(),width);}
        PaintToolPointer(static_cast<ui::Icon>(tool));
        Label(std::to_wstring(tool==Tool::Pixelate?pixelSize:static_cast<int>(thickness))+L" px",D2D1::RectF(mouse.x+radius+8,mouse.y+radius+6,mouse.x+radius+80,mouse.y+radius+31),0xffffff,true);
    }
    void ShowUtility(bool menu){
        Hide();trayMenu=menu;settings=!menu;active=true;selected=false;status.clear();const int width=menu?280:680,height=menu?240:664;image={0,0,width,height,{}};image.pixels.assign(static_cast<size_t>(width)*height,0xff171e2b);
        POINT point{};GetCursorPos(&point);MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromPoint(point,MONITOR_DEFAULTTONEAREST),&monitor);
        const int x=menu?point.x:(monitor.rcWork.left+monitor.rcWork.right-width)/2,y=menu?point.y-height:(monitor.rcWork.top+monitor.rcWork.bottom-height)/2;
        SetWindowLongPtrW(window,GWL_STYLE,WS_POPUP|WS_CLIPCHILDREN);SetWindowPos(window,menu?HWND_TOPMOST:HWND_NOTOPMOST,std::clamp<LONG>(x,monitor.rcWork.left,std::max(monitor.rcWork.left,monitor.rcWork.right-width)),std::clamp<LONG>(y,monitor.rcWork.top,std::max(monitor.rcWork.top,monitor.rcWork.bottom-height)),width,height,SWP_FRAMECHANGED|SWP_SHOWWINDOW);SetForegroundWindow(window);SetFocus(window);RefreshCursor(true);RequestFrame();
    }
    void ShowTrayMenu(){ShowUtility(true);}
    void ShowSettings(){ShowUtility(false);ReadStartup();}
    D2D1_RECT_F TrayRow(int index)const{return D2D1::RectF(8,53+index*44.0f,272,93+index*44.0f);}
    D2D1_RECT_F StartupButton()const{return D2D1::RectF(574,430,636,470);}
    D2D1_RECT_F StartupOffButton()const{return D2D1::RectF(492,558,632,588);}
    D2D1_RECT_F FolderSettingButton()const{return D2D1::RectF(524,280,632,318);}
    D2D1_RECT_F HotkeySettingButton()const{return D2D1::RectF(524,182,632,220);}
    D2D1_RECT_F HotkeyValue()const{return D2D1::RectF(48,182,506,220);}
    void SettingsDown(D2D1_POINT_2F point){
        mouse=point;if(Inside(CloseButton(),point)){Hide();return;}if(choosingHotkey)status.clear();choosingHotkey=false;
        if(Inside(StartupButton(),point))SetStartup(!startupHere);
        else if(!startupCommand.empty()&&!startupHere&&Inside(StartupOffButton(),point))SetStartup(false);
        else if(Inside(FolderSettingButton(),point)){ChooseFolder();status=L"Папка сохранения: "+folder.wstring();}
        else if(Inside(HotkeySettingButton(),point)||Inside(HotkeyValue(),point)){choosingHotkey=true;status=L"Нажмите клавишу или сочетание, например Print Screen или Ctrl+Alt+S. Esc — отменить.";SetFocus(window);}
        RequestFrame();
    }
    void SettingsCard(D2D1_RECT_F bounds,ui::Icon icon,const wchar_t* title){
        SetColor(brush.Get(),0x202939);target->FillRoundedRectangle(D2D1::RoundedRect(bounds,12,12),brush.Get());SetColor(brush.Get(),0x303d52);target->DrawRoundedRectangle(D2D1::RoundedRect(bounds,12,12),brush.Get(),1);
        const auto tile=D2D1::RectF(bounds.left+20,bounds.top+20,bounds.left+56,bounds.top+56);SetColor(brush.Get(),0x30374e);target->FillRoundedRectangle(D2D1::RoundedRect(tile,9,9),brush.Get());Icon(icon,D2D1::RectF(tile.left+7,tile.top+7,tile.right-7,tile.bottom-7),0xc4b2ee);
        SetColor(brush.Get(),0xebf0fc);target->DrawTextW(title,static_cast<UINT32>(wcslen(title)),settingsSectionFont.Get(),D2D1::RectF(bounds.left+70,bounds.top+19,bounds.right-126,bounds.top+48),brush.Get());
    }
    void SettingsAction(D2D1_RECT_F bounds,const wchar_t* label,bool on=false,bool chevron=true){
        SetColor(brush.Get(),on?0x493b69:Inside(bounds,mouse)?0x3b4861:0x303d53);target->FillRoundedRectangle(D2D1::RoundedRect(bounds,7,7),brush.Get());SetColor(brush.Get(),on?0xa68adf:0x485870);target->DrawRoundedRectangle(D2D1::RoundedRect(bounds,7,7),brush.Get(),1);
        Label(label,D2D1::RectF(bounds.left+13,bounds.top+10,bounds.right-(chevron?25:8),bounds.bottom),0xf0eaff,true);if(chevron)Icon(ui::Icon::ChevronRight,D2D1::RectF(bounds.right-23,bounds.top+12,bounds.right-9,bounds.bottom-12),0xc4b2ee);
    }
    void SettingsPath(const std::wstring& path,D2D1_RECT_F bounds){
        SetColor(brush.Get(),0x171f2d);target->FillRoundedRectangle(D2D1::RoundedRect(bounds,6,6),brush.Get());const auto textBounds=D2D1::RectF(bounds.left+12,bounds.top+10,bounds.right-12,bounds.bottom-6);
        ComPtr<IDWriteTextLayout> layout;ComPtr<IDWriteInlineObject> ellipsis;
        if(SUCCEEDED(textFactory->CreateTextLayout(path.c_str(),static_cast<UINT32>(path.size()),smallFont.Get(),textBounds.right-textBounds.left,textBounds.bottom-textBounds.top,&layout))){layout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);if(SUCCEEDED(textFactory->CreateEllipsisTrimmingSign(smallFont.Get(),&ellipsis))){const DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER,L'\\',1};layout->SetTrimming(&trimming,ellipsis.Get());}SetColor(brush.Get(),0xbac9df);target->DrawTextLayout(D2D1::Point2F(textBounds.left,textBounds.top),layout.Get(),brush.Get(),D2D1_DRAW_TEXT_OPTIONS_CLIP);}
    }
    void PaintSettings(){
        if(!active)return;Resources();target->BeginDraw();target->Clear(D2D1::ColorF(0x151c29));
        SetColor(brush.Get(),0x302b46);target->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(28,24,76,72),12,12),brush.Get());Icon(ui::Icon::Settings,D2D1::RectF(39,35,65,61),0xcbb5fa);SetColor(brush.Get(),0xf1f3fc);const wchar_t* title=L"Настройки";target->DrawTextW(title,static_cast<UINT32>(wcslen(title)),settingsTitleFont.Get(),D2D1::RectF(92,22,614,58),brush.Get());Label(L"Sparkshot · захват, сохранение и запуск",D2D1::RectF(92,59,624,81),0x9baec9,true);Button(CloseButton());Icon(ui::Icon::Close,D2D1::RectF(644,20,660,36),0xbac9df);
        SetColor(brush.Get(),0x2c374b);target->DrawLine(D2D1::Point2F(28,94),D2D1::Point2F(652,94),brush.Get(),1);
        SettingsCard(D2D1::RectF(28,108,652,242),ui::Icon::Keyboard,L"Захват экрана");Label(hotkeyAvailable?L"Клавиша запускает выделение области":L"Сочетание занято — выберите другое",D2D1::RectF(98,156,632,178),hotkeyAvailable?0x9baec9:0xffc857,true);
        const auto key=HotkeyValue();SetColor(brush.Get(),choosingHotkey?0x362d4b:0x171f2d);target->FillRoundedRectangle(D2D1::RoundedRect(key,7,7),brush.Get());SetColor(brush.Get(),choosingHotkey?0xa68adf:0x36455c);target->DrawRoundedRectangle(D2D1::RoundedRect(key,7,7),brush.Get(),1);Icon(ui::Icon::Keyboard,D2D1::RectF(60,193,78,211),0xa99cc8);Label(choosingHotkey?L"Нажмите новое сочетание…":HotkeyName(),D2D1::RectF(91,192,492,218),0xebf0fc,true);SettingsAction(HotkeySettingButton(),L"Изменить",choosingHotkey);
        SettingsCard(D2D1::RectF(28,258,652,396),ui::Icon::Folder,L"Сохранение");Label(L"Папка для снимков · Screenshot_номер.png",D2D1::RectF(98,306,504,328),0x9baec9,true);SettingsAction(FolderSettingButton(),L"Выбрать");SettingsPath(folder.wstring(),D2D1::RectF(48,338,632,378));
        SettingsCard(D2D1::RectF(28,412,652,600),ui::Icon::Startup,L"Автозапуск");Label(startupHere?L"Включён · запускать эту копию при входе в Windows":startupCommand.empty()?L"Выключен · включите переключатель справа":L"Другая копия · включите для замены пути",D2D1::RectF(98,460,634,484),0x9baec9,true);
        const auto toggle=D2D1::RectF(584,438,632,464);SetColor(brush.Get(),startupHere?0x9f7fe3:Inside(StartupButton(),mouse)?0x52617b:0x3d4b64);target->FillRoundedRectangle(D2D1::RoundedRect(toggle,13,13),brush.Get());SetColor(brush.Get(),0xf3edff);target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(startupHere?619.0f:597.0f,451),9,9),brush.Get());
        Label(L"Расположение программы",D2D1::RectF(48,494,632,515),0x91a5c0,true);SettingsPath(executable.wstring(),D2D1::RectF(48,518,632,554));
        Label(!startupCommand.empty()&&!startupHere?L"Старую привязку можно отключить.":L"Тихий запуск в трее, без снимка экрана",D2D1::RectF(48,565,!startupCommand.empty()&&!startupHere?484.0f:632.0f,590),0x91a5c0,true);if(!startupCommand.empty()&&!startupHere)SettingsAction(StartupOffButton(),L"Отключить",false,false);
        Icon(ui::Icon::Info,D2D1::RectF(28,620,48,640),status.empty()?0x7e94b6:0xffc857);Label(status.empty()?L"Изменения сохраняются сразу · портативная версия 0.1.8":status,D2D1::RectF(62,614,652,658),status.empty()?0x91a5c0:0xffc857,true);
        PresentFrame(target->EndDraw());
    }
    void PaintTray(){
        Resources();target->BeginDraw();target->Clear(D2D1::ColorF(0x171e2b));Icon(ui::Icon::Spark,D2D1::RectF(16,15,40,39),0xc4acff);Label(L"Sparkshot",D2D1::RectF(51,15,250,45),0xe3ebf7,true);
        const std::array<std::wstring,4> labels{L"Снимок · "+HotkeyName(),L"Настройки",L"Папка сохранения",L"Выход"};
        for(int i=0;i<4;++i){const auto r=TrayRow(i);Button(r);Icon(i==0?ui::Icon::Rectangle:i==1?ui::Icon::Settings:i==2?ui::Icon::Folder:ui::Icon::Exit,D2D1::RectF(20,r.top+9,42,r.top+31));Label(labels[i],D2D1::RectF(56,r.top+11,263,r.bottom),0xd2def0,true);}
        PresentFrame(target->EndDraw());
    }
    static LRESULT CALLBACK Procedure(HWND hwnd,UINT message,WPARAM wparam,LPARAM lparam){
        auto* self=reinterpret_cast<Impl*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
        if(message==WM_NCCREATE){self=static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);self->window=hwnd;SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
        if(!self)return DefWindowProcW(hwnd,message,wparam,lparam);
        try{
            switch(message){
            case CaptureMessage:self->Capture();return 0;
            case FrameMessage:return 0;
            case WM_HOTKEY:if(self->settings&&self->choosingHotkey)self->HotkeyInput(self->captureKey);else self->Capture();return 0;
            case WM_PAINT:{PAINTSTRUCT paint{};BeginPaint(hwnd,&paint);EndPaint(hwnd,&paint);self->RequestFrame();return 0;}
            case WM_PRINT:
            case WM_PRINTCLIENT:{
                if(!self->active)return 0;
                if(!self->target)return 0;
                const auto pixels=self->renderer.ReadPixels();BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=self->image.width;info.bmiHeader.biHeight=-self->image.height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
                return SetDIBitsToDevice(reinterpret_cast<HDC>(wparam),0,0,self->image.width,self->image.height,0,0,0,self->image.height,pixels.data(),&info,DIB_RGB_COLORS);
            }
            case WM_ERASEBKGND:return 1;
            case WM_LBUTTONDOWN:{const auto point=D2D1::Point2F(static_cast<float>(GET_X_LPARAM(lparam)),static_cast<float>(GET_Y_LPARAM(lparam)));if(self->settings)self->SettingsDown(point);else if(self->trayMenu){for(int row=0;row<4;++row)if(Inside(self->TrayRow(row),point)){if(row==0)self->Capture();else if(row==1)self->ShowSettings();else if(row==2){self->ShowSettings();self->ChooseFolder();}else DestroyWindow(hwnd);break;}}else self->Down(point);self->RefreshCursor();return 0;}
            case WM_MOUSEMOVE:if(!self->mouseInside){self->mouseInside=true;TRACKMOUSEEVENT tracking{sizeof(tracking),TME_LEAVE,hwnd,0};if(!self->headless)TrackMouseEvent(&tracking);}if(self->trayMenu||self->settings){self->mouse=D2D1::Point2F(static_cast<float>(GET_X_LPARAM(lparam)),static_cast<float>(GET_Y_LPARAM(lparam)));self->RequestFrame();}else self->Move(D2D1::Point2F(static_cast<float>(GET_X_LPARAM(lparam)),static_cast<float>(GET_Y_LPARAM(lparam))));self->RefreshCursor();return 0;
            case WM_MOUSELEAVE:self->mouseInside=false;self->hover=-1;self->RequestFrame();return 0;
            case WM_SETCURSOR:if(LOWORD(lparam)==HTCLIENT){if(reinterpret_cast<HWND>(wparam)!=hwnd)return FALSE;self->RefreshCursor(true);return TRUE;}break;
            case WM_LBUTTONUP:self->Up(D2D1::Point2F(static_cast<float>(GET_X_LPARAM(lparam)),static_cast<float>(GET_Y_LPARAM(lparam))));self->RefreshCursor();return 0;
            case WM_KEYDOWN:if(self->settings&&self->choosingHotkey)self->HotkeyInput(wparam);else{self->Modifier(wparam,true);self->Key(wparam);}self->RefreshCursor();return 0;
            case WM_KEYUP:if(self->settings&&self->choosingHotkey&&wparam==VK_SNAPSHOT)self->HotkeyInput(wparam);else self->Modifier(wparam,false);return 0;
            case WM_RBUTTONUP:if(self->active&&!self->trayMenu&&!self->settings&&!self->dragging&&self->selected){self->mouse=D2D1::Point2F(static_cast<float>(GET_X_LPARAM(lparam)),static_cast<float>(GET_Y_LPARAM(lparam)));if(!self->palette)self->OpenPalette(self->mouse);}self->RefreshCursor();return 0;
            case WM_CONTEXTMENU:return 0;
            case WM_COMMAND:if(reinterpret_cast<HWND>(lparam)==self->hexEdit&&HIWORD(wparam)==EN_CHANGE)self->ReadHex();else if(reinterpret_cast<HWND>(lparam)==self->edit&&HIWORD(wparam)==EN_CHANGE)self->ReadText();return 0;
            case WM_TIMER:if(wparam==1&&self->edit){self->caretVisible=!self->caretVisible;self->RequestFrame();}return 0;
            case WM_CTLCOLOREDIT:{const HDC dc=reinterpret_cast<HDC>(wparam);SetTextColor(dc,RGB(223,233,249));SetBkColor(dc,RGB(32,39,53));return reinterpret_cast<LRESULT>(self->inputBackground);}
            case WM_ACTIVATE:if(LOWORD(wparam)==WA_INACTIVE){self->picker=false;self->snapShapes=false;self->guides=false;if(self->choosingHotkey){self->choosingHotkey=false;self->status=L"Выбор клавиши отменён.";}self->RequestFrame();}else if(self->active){self->snapShapes=(GetKeyState(VK_SHIFT)&0x8000)!=0;self->guides=(GetKeyState(VK_TAB)&0x8000)!=0;self->RefreshCursor(true);self->RequestFrame();}break;
            case WM_SYSKEYDOWN:if(self->settings&&self->choosingHotkey){self->HotkeyInput(wparam);return 0;}if(wparam==VK_MENU||wparam==VK_SHIFT||wparam==VK_TAB){self->Modifier(wparam,true);return 0;}break;
            case WM_SYSKEYUP:if(wparam==VK_MENU||wparam==VK_SHIFT||wparam==VK_TAB){self->Modifier(wparam,false);return 0;}break;
            case WM_MOUSEWHEEL:self->Wheel(GET_WHEEL_DELTA_WPARAM(wparam),GET_KEYSTATE_WPARAM(wparam));self->RefreshCursor();return 0;
            case WM_SIZE:if(self->target){RECT bounds{};GetClientRect(hwnd,&bounds);const auto size=self->target->GetPixelSize();if(size.width!=static_cast<UINT>(bounds.right)||size.height!=static_cast<UINT>(bounds.bottom)){self->DropRenderResources();self->RequestFrame();}}return 0;
            case WM_NCHITTEST:if(self->settings){POINT point{GET_X_LPARAM(lparam),GET_Y_LPARAM(lparam)};ScreenToClient(hwnd,&point);if(point.y>=0&&point.y<94&&point.x>=0&&point.x<self->image.width-52)return HTCAPTION;}break;
            case WM_CLOSE:if(self->demo)DestroyWindow(hwnd);else if(self->settings||self->trayMenu)self->Hide();else self->FinishCapture();return 0;
            case TrayMessage:
                if(lparam==WM_LBUTTONDBLCLK)self->Capture();
                else if(lparam==WM_RBUTTONUP)self->ShowTrayMenu();return 0;
            case WM_DESTROY:{UnregisterHotKey(hwnd,1);NOTIFYICONDATAW icon{sizeof(icon)};icon.hWnd=hwnd;icon.uID=1;Shell_NotifyIconW(NIM_DELETE,&icon);PostQuitMessage(0);return 0;}
            }
        }catch(const std::exception& error){self->Error(error);}
        return DefWindowProcW(hwnd,message,wparam,lparam);
    }
};
void App::Impl::Save(){if(!selected)return;const auto file=SaveScreenshot(folder,EncodePng(Export()));NOTIFYICONDATAW icon{sizeof(icon)};icon.hWnd=window;icon.uID=1;icon.uFlags=NIF_TIP;const std::wstring tip=L"Sparkshot — "+file.filename().wstring();wcsncpy_s(icon.szTip,tip.c_str(),_TRUNCATE);Shell_NotifyIconW(NIM_MODIFY,&icon);FinishCapture();}
App::App(HINSTANCE instance,std::wstring command):impl_(std::make_unique<Impl>()){impl_->instance=instance;impl_->command=std::move(command);}
App::~App()=default;
int App::Run(){
    impl_->demo=impl_->command.find(L"--demo")!=std::wstring::npos;impl_->headless=impl_->demo&&impl_->command.find(L"--headless")!=std::wstring::npos;
    const bool autostart=impl_->command.find(L"--autostart")!=std::wstring::npos;
    if(!impl_->demo){const HWND existing=FindWindowW(L"Sparkshot.Native",nullptr);if(existing){if(!autostart)PostMessageW(existing,CaptureMessage,0,0);return 0;}}
    impl_->LoadSettings();
    impl_->inputFont=CreateFontW(-16,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    if(FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,impl_->factory.GetAddressOf())))return 2;
    // Однократная инициализация при запуске: клавиша захвата не пересоздаёт графическое устройство.
    try{impl_->renderer.Initialize(impl_->factory.Get(),impl_->headless&&impl_->command.find(L"--warp")!=std::wstring::npos);}catch(const std::exception&){return 2;}
    impl_->factory->CreateStrokeStyle(D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND,D2D1_CAP_STYLE_ROUND,D2D1_CAP_STYLE_ROUND,D2D1_LINE_JOIN_ROUND),nullptr,0,&impl_->lineStyle);
    if(FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(impl_->textFactory.GetAddressOf()))))return 2;
    impl_->textFactory->CreateTextFormat(L"Segoe UI",nullptr,DWRITE_FONT_WEIGHT_SEMI_BOLD,DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,21,L"ru-ru",&impl_->font);
    impl_->textFactory->CreateTextFormat(L"Segoe UI",nullptr,DWRITE_FONT_WEIGHT_NORMAL,DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,14,L"ru-ru",&impl_->smallFont);
    impl_->textFactory->CreateTextFormat(L"Segoe UI",nullptr,DWRITE_FONT_WEIGHT_SEMI_BOLD,DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,26,L"ru-ru",&impl_->settingsTitleFont);
    impl_->textFactory->CreateTextFormat(L"Segoe UI",nullptr,DWRITE_FONT_WEIGHT_SEMI_BOLD,DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,17,L"ru-ru",&impl_->settingsSectionFont);
    WNDCLASSW type{};type.hInstance=impl_->instance;type.lpfnWndProc=Impl::Procedure;type.lpszClassName=L"Sparkshot.Native";type.hCursor=impl_->cursors.Arrow();type.hIcon=LoadIconW(impl_->instance,MAKEINTRESOURCEW(101));RegisterClassW(&type);
    const HWND window=CreateWindowExW(WS_EX_APPWINDOW,type.lpszClassName,L"Sparkshot",WS_POPUP|WS_CLIPCHILDREN,0,0,100,100,nullptr,nullptr,impl_->instance,impl_.get());if(!window)return 3;
    NOTIFYICONDATAW icon{sizeof(icon)};icon.hWnd=window;icon.uID=1;icon.uFlags=NIF_ICON|NIF_MESSAGE|NIF_TIP;icon.uCallbackMessage=TrayMessage;icon.hIcon=type.hIcon;wcscpy_s(icon.szTip,L"Sparkshot — Ctrl+Alt+S");if(!impl_->headless)Shell_NotifyIconW(NIM_ADD,&icon);
    if(!impl_->demo)impl_->hotkeyAvailable=RegisterHotKey(window,1,impl_->captureModifiers|MOD_NOREPEAT,impl_->captureKey)!=FALSE;impl_->UpdateTrayTip();
    if(!autostart)PostMessageW(window,CaptureMessage,0,0);MSG message{};
    bool quit=false;
    const auto dispatch=[&](MSG& next){if(next.message==WM_QUIT){message=next;quit=true;return;}TranslateMessage(&next);DispatchMessageW(&next);};
    while(!quit){
        if(impl_->active&&impl_->framePending){
            try{impl_->Resources();}catch(const std::exception& error){impl_->DropRenderResources(true);impl_->Error(error);impl_->framePending=false;continue;}
            HANDLE ready=impl_->renderer.FrameReady();const DWORD result=impl_->frameSlotReady?WAIT_OBJECT_0:MsgWaitForMultipleObjectsEx(1,&ready,impl_->retryPresent?4:INFINITE,QS_ALLINPUT,MWMO_INPUTAVAILABLE);
            if(result==WAIT_OBJECT_0||(result==WAIT_TIMEOUT&&impl_->retryPresent))impl_->frameSlotReady=true;
            // Ввод обрабатывается перед готовым кадром; размер очереди сообщений не определяет FPS.
            MSG next{};for(int count=0;count<64&&PeekMessageW(&next,nullptr,0,0,PM_REMOVE);++count){dispatch(next);if(quit)break;}
            if(!quit&&impl_->frameSlotReady&&impl_->active&&impl_->framePending){
                try{impl_->PaintFrame();}catch(const std::exception& error){impl_->DropRenderResources(true);impl_->Error(error);impl_->framePending=false;}
            }
            if(result==WAIT_FAILED){impl_->framePending=false;}
        }else{const int result=GetMessageW(&message,nullptr,0,0);if(result<=0)break;dispatch(message);}
    }
    return static_cast<int>(message.wParam);
}
}
