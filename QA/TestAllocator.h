#pragma once
#include <AK/AkWwiseSDKVersion.h>
#include <AK/SoundEngine/Common/IAkPluginMemAlloc.h>
#include <malloc.h>

struct TestAllocator final : AK::IAkPluginMemAlloc {
    size_t live = 0, calls = 0;
// SDK 2019.2.15's allocator virtual interface differs from the tested 2023/2025
// SDKs: Malloc/Malign omit debug arguments; debug methods are separate and there
// are no Realloc slots here. Compile QA against the target SDK so a modern
// allocator vtable cannot falsely validate an old runtime DLL's ABI.
#if AK_WWISESDK_VERSION_MAJOR == 2019
    void *Malloc(size_t n) override {
        ++live;
        ++calls;
        return _aligned_malloc(n, 16);
    }
    void *Malign(size_t n, size_t a) override {
        ++live;
        ++calls;
        return _aligned_malloc(n, a);
    }
#if defined(AK_MEMDEBUG)
    void *dMalloc(size_t n, const char *, AkUInt32) override { return Malloc(n); }
    void *dMalign(size_t n, size_t a, const char *, AkUInt32) override { return Malign(n, a); }
#endif
#else
    void *Malloc(size_t n, const char *, AkUInt32) override {
        ++live;
        ++calls;
        return _aligned_malloc(n, 16);
    }
    void *Malign(size_t n, size_t a, const char *, AkUInt32) override {
        ++live;
        ++calls;
        return _aligned_malloc(n, a);
    }
    void *Realloc(void *p, size_t n, const char *, AkUInt32) override {
        if (!p)
            ++live;
        ++calls;
        return _aligned_realloc(p, n, 16);
    }
    void *ReallocAligned(void *p, size_t n, size_t a, const char *, AkUInt32) override {
        if (!p)
            ++live;
        ++calls;
        return _aligned_realloc(p, n, a);
    }
#endif
    void Free(void *p) override {
        if (p) {
            --live;
            _aligned_free(p);
        }
    }
};
