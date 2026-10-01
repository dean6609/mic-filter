#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <algorithm>
#include <string>

namespace micfilter {
inline constexpr wchar_t kClsidText[] = L"{CDB2B27A-3B40-4B79-95AA-123C7136D873}";
inline constexpr GUID kClsid = {0xcdb2b27a,0x3b40,0x4b79,{0x95,0xaa,0x12,0x3c,0x71,0x36,0xd8,0x73}};
inline constexpr LONG kMagic = 0x5741564f;
inline constexpr LONG kStateVersion = 1;
inline constexpr wchar_t kWindowClass[] = L"MicFilter.Native.Tray.v1";
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
    const auto current=std::wstring(buffer)+L"\\MicFilter";
    const auto legacy=std::wstring(buffer)+L"\\WavoFilter";
    // Existing installations keep their shared control file during the rename.
    if(GetFileAttributesW((current+L"\\state.bin").c_str())==INVALID_FILE_ATTRIBUTES&&GetFileAttributesW((legacy+L"\\state.bin").c_str())!=INVALID_FILE_ATTRIBUTES)return legacy;
    return current;
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
