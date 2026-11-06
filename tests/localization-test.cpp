#include "localization.hpp"
#include <windows.h>

static BOOL CALLBACK CollectLanguage(HMODULE, LPCWSTR, LPCWSTR, WORD language, LONG_PTR context) {
    auto& mask=*reinterpret_cast<unsigned*>(context);
    if (language==MAKELANGID(LANG_ENGLISH,SUBLANG_ENGLISH_US)) mask|=1;
    if (language==MAKELANGID(LANG_RUSSIAN,SUBLANG_DEFAULT)) mask|=2;
    return TRUE;
}

int main() {
    using namespace sparkshot;
    if (ChooseLanguage(L"", L"ru-RU") != Language::Russian) return 1;
    if (ChooseLanguage(L"", L"en-US") != Language::English) return 2;
    if (ChooseLanguage(L"", L"fr-FR") != Language::English) return 3;
    if (ChooseLanguage(L"en", L"ru-RU") != Language::English) return 4;
    if (ChooseLanguage(L"ru", L"en-US") != Language::Russian) return 5;
    if (ChooseLanguage(L"invalid", L"ru-RU") != Language::Russian) return 6;
    if (LoadLocalizedString(GetModuleHandleW(nullptr), Language::English, TextId::Settings) != L"Settings") return 10;
    if (LoadLocalizedString(GetModuleHandleW(nullptr), Language::Russian, TextId::Settings) != L"Настройки") return 11;
    if (LoadLocalizedString(GetModuleHandleW(nullptr), Language::English, TextId::ErrorTheClipboardIsInUseByAnotherApplication) != L"The clipboard is in use by another application") return 12;
    if (LoadLocalizedString(GetModuleHandleW(nullptr), Language::Russian, TextId::ErrorTheClipboardIsInUseByAnotherApplication) != L"Буфер обмена занят другим приложением") return 13;
    if (FormatLocalized(L"Press {0} to capture",L"F12") != L"Press F12 to capture") return 16;
    if (FormatLocalized(L"{0} / {0}",L"S") != L"S / S") return 17;
    for (auto id : {TextId::CaptureShortcutSaved,TextId::IsInUseCaptureIsAvailableFromTheSparkshotTrayIco,TextId::ShiftShapeWheelSizeAltEyedropperCtrlZCtrlYCtrlSh,TextId::ScreenCapture,TextId::SaveFolder,TextId::Capture}) {
        if (LoadLocalizedString(GetModuleHandleW(nullptr),Language::English,id).find(L"{0}")==std::wstring::npos) return 18;
        if (LoadLocalizedString(GetModuleHandleW(nullptr),Language::Russian,id).find(L"{0}")==std::wstring::npos) return 19;
    }
    for (size_t i=0;i<TextCount;++i) {
        auto id=static_cast<TextId>(FirstTextId+i);
        if (LoadLocalizedString(GetModuleHandleW(nullptr),Language::English,id).empty()) return 14;
        if (LoadLocalizedString(GetModuleHandleW(nullptr),Language::Russian,id).empty()) return 15;
    }
    for (unsigned block=(FirstTextId>>4)+1;block<=((FirstTextId+static_cast<unsigned>(TextCount)-1)>>4)+1;++block) {
        unsigned languages=0;
        if (!EnumResourceLanguagesW(GetModuleHandleW(nullptr),RT_STRING,MAKEINTRESOURCEW(block),CollectLanguage,reinterpret_cast<LONG_PTR>(&languages)) || languages!=3) return 20;
    }
}
