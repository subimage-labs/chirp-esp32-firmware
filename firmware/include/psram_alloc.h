// psram_alloc.h — a standard allocator that takes memory from PSRAM.
//
// The ESP32 heap serves requests below CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL
// (4096 B) from internal RAM even when PSRAM is free, so many small strings
// (a chat transcript) eat the scarce internal heap and fragment it. Containers
// built on PsramAlloc keep every block, small or large, in PSRAM; they fall
// back to the ordinary heap only when PSRAM is exhausted or absent. Off the
// device (host tests) it is plain malloc. Stateless: all instances compare equal.
// Allocations are not thread-bound; a container belongs to one context.
#pragma once

#include <stddef.h>
#include <stdlib.h>

#include <string>
#include <vector>

#if defined(ESP_PLATFORM) && __has_include(<esp_heap_caps.h>)
#include <esp_heap_caps.h>
#define PSRAM_ALLOC_DEVICE 1
#endif

inline void* psramAllocBytes(size_t n) {
#ifdef PSRAM_ALLOC_DEVICE
    if (void* p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)) return p;
#endif
    void* p = malloc(n);
    if (!p) abort();                              // as operator new does without exceptions
    return p;
}

template <class T>
struct PsramAlloc {
    typedef T value_type;
    PsramAlloc() {}
    template <class U>
    PsramAlloc(const PsramAlloc<U>&) {}
    T* allocate(size_t n) { return static_cast<T*>(psramAllocBytes(n * sizeof(T))); }
    void deallocate(T* p, size_t) { free(p); }    // heap_caps_malloc blocks are free()-able
};
template <class T, class U>
inline bool operator==(const PsramAlloc<T>&, const PsramAlloc<U>&) { return true; }
template <class T, class U>
inline bool operator!=(const PsramAlloc<T>&, const PsramAlloc<U>&) { return false; }

typedef std::basic_string<char, std::char_traits<char>, PsramAlloc<char> > PsString;
template <class T>
using PsVector = std::vector<T, PsramAlloc<T> >;
