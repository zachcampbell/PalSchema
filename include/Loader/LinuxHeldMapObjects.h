#pragma once
#ifdef __linux__
#include "Unreal/NameTypes.hpp"
namespace Palworld::LinuxHeldMapObjects {
    void NoteRegisteredId(const RC::Unreal::FName& id);
    // Game thread, after the building loaders have registered their ids. Writes the ids for the next boot's
    // libpalhold, then has libpalhold apply the saved map objects it held back at boot. Returns how many were
    // applied, or -1 if libpalhold is not preloaded or not installed.
    int ReleaseHeld();
}
#endif
