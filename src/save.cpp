#include "save.hpp"
#include <windows.h>
#include <algorithm>
#include <limits>
#include <stdexcept>
namespace sparkshot {
std::filesystem::path SaveScreenshot(const std::filesystem::path& directory,const std::vector<std::uint8_t>& png){
    std::filesystem::create_directories(directory);
    std::uint64_t next=1;
    for(const auto& entry:std::filesystem::directory_iterator(directory)){
        if(!entry.is_regular_file())continue;
        auto name=entry.path().stem().wstring();
        if(name.size()<=11||_wcsnicmp(name.c_str(),L"Screenshot_",11)!=0)continue;
        std::uint64_t number=0;bool valid=true;
        for(size_t i=11;i<name.size();++i){const wchar_t digit=name[i];if(digit<L'0'||digit>L'9'||number>(std::numeric_limits<std::uint64_t>::max()-9)/10){valid=false;break;}number=number*10+digit-L'0';}
        if(valid){if(number==std::numeric_limits<std::uint64_t>::max())throw std::runtime_error("Исчерпана нумерация скриншотов");next=std::max(next,number+1);}
    }
    for(;;){
        const auto path=directory/(L"Screenshot_"+std::to_wstring(next)+L".png");
        // CREATE_NEW защищает чужие файлы даже при одновременном сохранении другим приложением.
        const HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE|DELETE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file==INVALID_HANDLE_VALUE){const DWORD error=GetLastError();if(error==ERROR_FILE_EXISTS||error==ERROR_ALREADY_EXISTS){if(next==std::numeric_limits<std::uint64_t>::max())throw std::runtime_error("Исчерпана нумерация скриншотов");++next;continue;}throw std::runtime_error("Не удалось создать файл. Проверьте папку и права доступа");}
        size_t offset=0;bool ok=true;
        while(offset<png.size()){const DWORD chunk=static_cast<DWORD>(std::min<size_t>(png.size()-offset,1<<20));DWORD written{};if(!WriteFile(file,png.data()+offset,chunk,&written,nullptr)||written==0){ok=false;break;}offset+=written;}
        if(!ok){FILE_DISPOSITION_INFO disposition{TRUE};SetFileInformationByHandle(file,FileDispositionInfo,&disposition,sizeof(disposition));CloseHandle(file);throw std::runtime_error("Не удалось записать PNG. Возможно, закончилось место на диске");}
        CloseHandle(file);return path;
    }
}
}
