#include "cursor.hpp"
namespace sparkshot {
HCURSOR NativeCursors::Arrow()const{return LoadCursorW(nullptr,IDC_ARROW);}
HCURSOR NativeCursors::Hand()const{return LoadCursorW(nullptr,IDC_HAND);}
}
