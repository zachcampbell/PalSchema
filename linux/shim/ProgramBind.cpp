// palhook: UE4SSProgram::s_program is a `static inline` private member, so a mod built with
// -fvisibility=hidden gets its own null copy and UE4SSProgram::get_program() dereferences nullptr. Bind
// the mod's copy to the library's exported one (dynsym `_ZN2RC12UE4SSProgram9s_programE`) before anything
// calls get_program(). Access to the private static goes through an explicit template instantiation,
// which the standard exempts from access checking.
#include <UE4SSProgram.hpp>
#include <dlfcn.h>
#include <cstdio>
namespace
{
    struct ProgramSlotTag { using type = RC::UE4SSProgram**; friend type steal(ProgramSlotTag); };
    template <typename Tag, typename Tag::type M> struct Rob { friend typename Tag::type steal(Tag) { return M; } };
    template struct Rob<ProgramSlotTag, &RC::UE4SSProgram::s_program>;
}
extern "C" void palhook_bind_program()
{
    RC::UE4SSProgram** mod_slot = steal(ProgramSlotTag{});
    auto** lib_slot = static_cast<RC::UE4SSProgram**>(dlsym(RTLD_DEFAULT, "_ZN2RC12UE4SSProgram9s_programE"));
    if (lib_slot && *lib_slot) *mod_slot = *lib_slot;
    std::fprintf(stderr, "[PalSchema] program singleton bound: lib=%p mod=%p\n", static_cast<void*>(lib_slot ? *lib_slot : nullptr), static_cast<void*>(*mod_slot));
}
