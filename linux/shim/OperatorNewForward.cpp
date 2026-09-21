// palhook (Linux port): the mod links libstdc++ and libgcc statically with -Bsymbolic so that every throw,
// catch and unwind inside it uses one consistent runtime (a throw that resolves to the game's or Steam's
// __cxa_throw segfaults: shadow runs 42 and 51). A static libstdc++ would also bring its own operator new
// over libc malloc, which must not happen: memory crosses into libUE4SS, whose operator new is the game's
// exported one, i.e. the engine allocator. So every operator new/delete here forwards to the game's exported
// _Znwm/_ZdlPv (looked up once through the global scope, where the mod's own copies are hidden), and the
// aligned forms go through UE4SS's FMemory, which is engine-routed and alignment-aware.
#include <Unreal/Core/HAL/UnrealMemory.hpp>
#include <dlfcn.h>
#include <new>
#include <cstdlib>
#include <cstdio>
namespace
{
    using NewFn = void* (*)(std::size_t);
    using DelFn = void (*)(void*);
    NewFn game_new()
    {
        static NewFn fn = reinterpret_cast<NewFn>(dlsym(RTLD_DEFAULT, "_Znwm"));
        if (!fn) { std::fprintf(stderr, "[PalSchema] fatal: game does not export operator new\n"); std::abort(); }
        return fn;
    }
    DelFn game_delete()
    {
        static DelFn fn = reinterpret_cast<DelFn>(dlsym(RTLD_DEFAULT, "_ZdlPv"));
        if (!fn) { std::fprintf(stderr, "[PalSchema] fatal: game does not export operator delete\n"); std::abort(); }
        return fn;
    }
}
void* operator new(std::size_t n) { return game_new()(n ? n : 1); }
void* operator new[](std::size_t n) { return game_new()(n ? n : 1); }
void* operator new(std::size_t n, const std::nothrow_t&) noexcept { return game_new()(n ? n : 1); }
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept { return game_new()(n ? n : 1); }
void operator delete(void* p) noexcept { if (p) game_delete()(p); }
void operator delete[](void* p) noexcept { if (p) game_delete()(p); }
void operator delete(void* p, std::size_t) noexcept { if (p) game_delete()(p); }
void operator delete[](void* p, std::size_t) noexcept { if (p) game_delete()(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { if (p) game_delete()(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { if (p) game_delete()(p); }
void* operator new(std::size_t n, std::align_val_t a) { return RC::Unreal::FMemory::Malloc(n ? n : 1, static_cast<uint32_t>(a)); }
void* operator new[](std::size_t n, std::align_val_t a) { return RC::Unreal::FMemory::Malloc(n ? n : 1, static_cast<uint32_t>(a)); }
void* operator new(std::size_t n, std::align_val_t a, const std::nothrow_t&) noexcept { return RC::Unreal::FMemory::Malloc(n ? n : 1, static_cast<uint32_t>(a)); }
void* operator new[](std::size_t n, std::align_val_t a, const std::nothrow_t&) noexcept { return RC::Unreal::FMemory::Malloc(n ? n : 1, static_cast<uint32_t>(a)); }
void operator delete(void* p, std::align_val_t) noexcept { if (p) RC::Unreal::FMemory::Free(p); }
void operator delete[](void* p, std::align_val_t) noexcept { if (p) RC::Unreal::FMemory::Free(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept { if (p) RC::Unreal::FMemory::Free(p); }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { if (p) RC::Unreal::FMemory::Free(p); }
