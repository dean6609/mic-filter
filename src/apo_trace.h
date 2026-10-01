#pragma once
#include "shared.h"
#include <evntprov.h>

// Lightweight ETW trace of APO lifecycle events (never called from APOProcess).
#include <sstream>

namespace micfilter {
inline constexpr GUID kTraceProvider={0x7e40186c,0xa78b,0x4d7a,{0x93,0x71,0x66,0x2b,0x98,0x91,0x54,0x03}};
class Trace {
    REGHANDLE handle_=0;
public:
    Trace(){EventRegister(&kTraceProvider,nullptr,nullptr,&handle_);}
    ~Trace(){if(handle_)EventUnregister(handle_);}
    void write(const std::wstring& message){if(handle_)EventWriteString(handle_,4,1,message.c_str());}
};
}
