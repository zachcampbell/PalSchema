#ifdef __linux__
// palhook: a Linux dedicated server applies its world save ~5 s after launch, but UE4SS cannot start before 30 s,
// so every saved map object whose MapObjectId comes from a PalSchema building mod used to fail its init process at
// boot, and a failed init handle parks the init manager for good (no logins, no autosave; 152 wooden diagonal
// fences on 2026-09-24). libpalhold, preloaded next to libUE4SS, now holds those entries back at boot with their
// init handles still pending. This side tells it which ids to hold (for the next boot) and, once the buildings are
// registered, asks it to apply what it held.
#include "Loader/LinuxHeldMapObjects.h"
#include "UE4SSProgram.hpp"
#include "Utility/Logging.h"
#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>

using namespace RC;
using namespace RC::Unreal;

namespace Palworld::LinuxHeldMapObjects {
    static std::set<std::string> g_registeredIds;

    void NoteRegisteredId(const FName& id) { g_registeredIds.insert(to_string(id.ToString())); }

    static void WriteHeldIds()
    {
        auto dir = std::filesystem::path(UE4SSProgram::get_program().get_working_directory()) / "Mods" / "PalSchema";
        auto path = dir / "held-map-object-ids.txt";
        auto tmp = dir / "held-map-object-ids.txt.tmp";
        {
            std::ofstream out(tmp, std::ios::trunc);
            out << "# Written by PalSchema at every boot: building ids whose saved map objects libpalhold holds until PalSchema has registered them.\n";
            for (auto& id : g_registeredIds) out << id << "\n";
            if (!out) { PS::Log<LogLevel::Warning>(STR("Linux: could not write {}.\n"), to_generic_string(tmp.string())); return; }
        }
        std::error_code ec;
        std::filesystem::rename(tmp, path, ec);
        if (ec) PS::Log<LogLevel::Warning>(STR("Linux: could not replace {}: {}.\n"), to_generic_string(path.string()), to_generic_string(ec.message()));
    }

    int ReleaseHeld()
    {
        WriteHeldIds();
        auto active = reinterpret_cast<int (*)()>(dlsym(RTLD_DEFAULT, "palhold_active"));
        auto release = reinterpret_cast<int (*)()>(dlsym(RTLD_DEFAULT, "palhold_release"));
        if (!active || !release)
        {
            if (!g_registeredIds.empty())
                PS::Log<LogLevel::Warning>(STR("Linux: libpalhold is not preloaded; saved pieces of PalSchema buildings will fail to load at the next boot and block logins.\n"));
            return -1;
        }
        if (!active())
        {
            PS::Log<LogLevel::Normal>(STR("Linux: libpalhold is preloaded but was not installed this boot (no id list yet, or a different game build); {} building ids written for the next boot.\n"), g_registeredIds.size());
            return -1;
        }
        int applied = release();
        PS::Log<LogLevel::Normal>(STR("Linux: libpalhold applied {} held map objects; {} building ids written for the next boot.\n"), applied, g_registeredIds.size());
        return applied;
    }
}
#endif
