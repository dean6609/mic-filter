#pragma once
#include <cstddef>
#include <cstdint>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/mman.h>
#endif
#if defined(__x86_64__) || defined(_M_X64) || defined(__SSE__)
#include <xmmintrin.h>
#endif

namespace micfilter {
// Keep audio state in physical memory so the callback never waits on a page fault.
// Failure is harmless: the memory simply stays pageable.
inline void lockMemory(void* address,std::size_t bytes) noexcept {
#if defined(_WIN32)
    VirtualLock(address,bytes);
#else
    mlock(address,bytes);
#endif
}
inline void unlockMemory(void* address,std::size_t bytes) noexcept {
#if defined(_WIN32)
    VirtualUnlock(address,bytes);
#else
    munlock(address,bytes);
#endif
}

// Filters and the network decay into denormal floats during silence, which can make each
// operation many times slower. Treat them as zero while processing and restore the caller's
// floating-point mode afterwards; the audio host's own threads are never left changed.
class DenormalGuard {
#if defined(__x86_64__) || defined(_M_X64) || defined(__SSE__)
    unsigned saved_;
public:
    DenormalGuard() noexcept:saved_(_mm_getcsr()){_mm_setcsr(saved_|0x8040);} // FTZ | DAZ
    ~DenormalGuard(){_mm_setcsr(saved_);}
#elif defined(__aarch64__)
    std::uint64_t saved_;
public:
    DenormalGuard() noexcept {
        __asm__ volatile("mrs %0, fpcr":"=r"(saved_));
        const std::uint64_t flushed=saved_|(std::uint64_t{1}<<24); // FZ
        __asm__ volatile("msr fpcr, %0"::"r"(flushed));
    }
    ~DenormalGuard(){__asm__ volatile("msr fpcr, %0"::"r"(saved_));}
#else
public:
    DenormalGuard() noexcept {}
#endif
    DenormalGuard(const DenormalGuard&)=delete;
    DenormalGuard& operator=(const DenormalGuard&)=delete;
};
}
