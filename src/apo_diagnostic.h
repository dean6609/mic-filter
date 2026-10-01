#pragma once
#include "shared.h"
#include <evntprov.h>
#include <sstream>

namespace wavo {
inline constexpr GUID kTraceProvider={0x7e40186c,0xa78b,0x4d7a,{0x93,0x71,0x66,0x2b,0x98,0x91,0x54,0x03}};
class Trace {
    REGHANDLE handle_=0;
public:
    Trace(){EventRegister(&kTraceProvider,nullptr,nullptr,&handle_);}
    ~Trace(){if(handle_)EventUnregister(handle_);}
    void write(const std::wstring& message){if(handle_)EventWriteString(handle_,4,1,message.c_str());}
};
struct alignas(8) Diagnostic {
    LONG magic,version;
    volatile LONG pid,stage,result,inputRate,channels,bytes,validBits,maxFrames,outputMaxFrames,rejection,inputFlags,outputFlags,frames,mappingError;
    alignas(8) volatile LONG64 callbacks,validCallbacks,lastTick;
    LONG reserved[10];
};
static_assert(sizeof(Diagnostic)==128);
class DiagnosticMapping {
    HANDLE file_=INVALID_HANDLE_VALUE,mapping_=nullptr;
    Diagnostic* state_=nullptr;
public:
    ~DiagnosticMapping(){if(state_)UnmapViewOfFile(state_);if(mapping_)CloseHandle(mapping_);if(file_!=INVALID_HANDLE_VALUE)CloseHandle(file_);}
    Diagnostic* get(){return state_;}
    bool open(){
        file_=CreateFileW((dataDirectory()+L"\\diagnostic.bin").c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr);
        if(file_==INVALID_HANDLE_VALUE)return false;
        LARGE_INTEGER length{};if(!GetFileSizeEx(file_,&length)||length.QuadPart!=sizeof(Diagnostic))return false;
        mapping_=CreateFileMappingW(file_,nullptr,PAGE_READWRITE,0,0,nullptr);if(!mapping_)return false;
        auto* p=static_cast<Diagnostic*>(MapViewOfFile(mapping_,FILE_MAP_READ|FILE_MAP_WRITE,0,0,sizeof(Diagnostic)));
        if(!p)return false;if(p->magic!=0x44415657||p->version!=1){UnmapViewOfFile(p);return false;}state_=p;return true;
    }
};
}
