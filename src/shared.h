#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <algorithm>
#include <string>

namespace wavo {
#ifdef WAVO_DIAGNOSTIC_CLSID
inline constexpr wchar_t kClsidText[] = L"{4A1F2290-FE8C-4F3A-9855-EEB7BC038171}";
inline constexpr GUID kClsid = {0x4a1f2290,0xfe8c,0x4f3a,{0x98,0x55,0xee,0xb7,0xbc,0x03,0x81,0x71}};
#else
inline constexpr wchar_t kClsidText[] = L"{54F530A1-D045-4C70-8999-11CF13E0DDAF}";
inline constexpr GUID kClsid = {0x54f530a1,0xd045,0x4c70,{0x89,0x99,0x11,0xcf,0x13,0xe0,0xdd,0xaf}};
#endif
inline constexpr LONG kMagic = 0x5741564f;
inline constexpr LONG kStateVersion = 1;
inline constexpr wchar_t kWindowClass[] = L"WavoFilter.Native.Tray.v1";
inline constexpr UINT kControlMessage = WM_APP + 20;

// One aligned, shared, fixed-size file. UI writes control fields; APO writes telemetry.
// Keep this ABI stable between the two binaries. Interlocked operations avoid locks in audio.
struct alignas(8) SharedState {
    LONG magic;
    LONG version;
    volatile LONG enabled;
    volatile LONG thresholdPermille;
    volatile LONG graceBlocks;
    volatile LONG wetPermille;
    volatile LONG channels;
    volatile LONG sampleRate;
    volatile LONG formatSupported;
    volatile LONG producerPid;
    alignas(8) volatile LONG64 callbacks;
    alignas(8) volatile LONG64 processedFrames;
    alignas(8) volatile LONG64 lastTick;
};
static_assert(sizeof(SharedState) == 64);

inline LONG read(volatile LONG& v) noexcept { return InterlockedCompareExchange(&v, 0, 0); }
inline LONG64 read(volatile LONG64& v) noexcept { return InterlockedCompareExchange64(&v, 0, 0); }
inline bool recentAudio(SharedState* p) noexcept {
    if(!p || !read(p->producerPid))return false;
    const auto tick=read(p->lastTick);const auto now=GetTickCount64();
    return tick>0 && now>=static_cast<ULONGLONG>(tick) && now-static_cast<ULONGLONG>(tick)<2500;
}

inline std::wstring dataDirectory() {
    wchar_t buffer[32768]{};
    const auto length = GetEnvironmentVariableW(L"ProgramData", buffer, 32768);
    if (!length || length >= 32768) return L"";
    return std::wstring(buffer) + L"\\WavoFilter";
}

class StateMapping {
    HANDLE file_ = INVALID_HANDLE_VALUE;
    HANDLE mapping_ = nullptr;
    SharedState* state_ = nullptr;
public:
    StateMapping() = default;
    StateMapping(const StateMapping&) = delete;
    StateMapping& operator=(const StateMapping&) = delete;
    ~StateMapping() { close(); }
    SharedState* get() const noexcept { return state_; }
    void close() noexcept {
        if (state_) UnmapViewOfFile(state_);
        if (mapping_) CloseHandle(mapping_);
        if (file_ != INVALID_HANDLE_VALUE) CloseHandle(file_);
        state_=nullptr; mapping_=nullptr; file_=INVALID_HANDLE_VALUE;
    }
    bool open() {
        close();
        const auto dir=dataDirectory();
        if (dir.empty()) return false;
        file_=CreateFileW((dir+L"\\state.bin").c_str(), GENERIC_READ|GENERIC_WRITE,
                         FILE_SHARE_READ|FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (file_==INVALID_HANDLE_VALUE) return false;
        LARGE_INTEGER length{};
        if (!GetFileSizeEx(file_, &length) || length.QuadPart != sizeof(SharedState)) { close(); return false; }
        mapping_=CreateFileMappingW(file_, nullptr, PAGE_READWRITE, 0, 0, nullptr);
        if (!mapping_) { close(); return false; }
        state_=static_cast<SharedState*>(MapViewOfFile(mapping_, FILE_MAP_READ|FILE_MAP_WRITE, 0, 0, sizeof(SharedState)));
        if (!state_ || state_->magic!=kMagic || state_->version!=kStateVersion) { close(); return false; }
        return true;
    }
};
struct Settings {
    bool enabled=false;
    float threshold=0.85f;
    unsigned grace=20;
    float wet=1.0f;
};
inline Settings snapshot(SharedState* p) noexcept {
    if (!p || p->magic!=kMagic || p->version!=kStateVersion) return {};
    return {read(p->enabled)!=0, std::clamp(read(p->thresholdPermille),0L,1000L)/1000.0f,
            static_cast<unsigned>(std::clamp(read(p->graceBlocks),0L,500L)),
            std::clamp(read(p->wetPermille),0L,1000L)/1000.0f};
}
}
