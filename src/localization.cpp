#include "localization.hpp"
#include <cwctype>

namespace sparkshot {
Language ChooseLanguage(std::wstring_view stored, std::wstring_view system) {
    auto is = [](std::wstring_view value, wchar_t a, wchar_t b) {
        return value.size() >= 2 && std::towlower(value[0]) == a && std::towlower(value[1]) == b
            && (value.size() == 2 || value[2] == L'-');
    };
    if (is(stored, L'r', L'u')) return Language::Russian;
    if (is(stored, L'e', L'n')) return Language::English;
    return is(system, L'r', L'u') ? Language::Russian : Language::English;
}
std::wstring FormatLocalized(std::wstring_view pattern, std::wstring_view value) {
    std::wstring result;size_t start=0;
    while (true) {
        const auto pos=pattern.find(L"{0}",start);
        if (pos==std::wstring_view::npos) {result.append(pattern.substr(start));return result;}
        result.append(pattern.substr(start,pos-start));result.append(value);start=pos+3;
    }
}
std::wstring LoadLocalizedString(HINSTANCE instance, Language language, TextId id) {
    const auto value=static_cast<unsigned>(id);
    const auto block=MAKEINTRESOURCEW((value >> 4)+1);
    const LANGID lang=language==Language::Russian?MAKELANGID(LANG_RUSSIAN,SUBLANG_DEFAULT):MAKELANGID(LANG_ENGLISH,SUBLANG_ENGLISH_US);
    HRSRC resource=FindResourceExW(instance,RT_STRING,block,lang);
    if(!resource&&language==Language::Russian)resource=FindResourceExW(instance,RT_STRING,block,MAKELANGID(LANG_ENGLISH,SUBLANG_ENGLISH_US));
    if(!resource)throw std::runtime_error("Missing localized string resource");
    const auto size=SizeofResource(instance,resource);const auto loaded=LoadResource(instance,resource);
    const auto* data=static_cast<const WORD*>(LockResource(loaded));
    if(!data||size%sizeof(WORD)!=0)throw std::runtime_error("Invalid localized string resource");
    const auto* end=data+size/sizeof(WORD);
    for(unsigned slot=0;slot<(value&15);++slot){if(data>=end||static_cast<size_t>(end-data-1)<*data)throw std::runtime_error("Invalid localized string resource");data+=*data+1;}
    if(data>=end||static_cast<size_t>(end-data-1)<*data||!*data)throw std::runtime_error("Missing localized string");
    return std::wstring(reinterpret_cast<const wchar_t*>(data+1),*data);
}
}
