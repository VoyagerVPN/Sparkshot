#pragma once
#include <d2d1.h>
namespace sparkshot {
// Общие системные указатели сохраняют выбранные пользователем тему и размер Windows.
class NativeCursors {
public:
    HCURSOR Arrow()const;
    HCURSOR Hand()const;
};
}
