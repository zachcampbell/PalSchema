#pragma once
#ifdef __linux__
#include <vector>
#include "Unreal/UObject.hpp"
#include "Unreal/UClass.hpp"

namespace Palworld::LinuxObjectIndex {
    // palhook: one walk of GUObjectArray per loader pass instead of one per edit. UE4SS's ForEachUObject and the
    // engine's GetObjectsOfClass both cost ~150 ms on this server (2M objects); Paldemonium paid it 115 times.
    void Invalidate();
    // Live (non-CDO) instances whose class is cls or derives from it. Built lazily after Invalidate().
    std::vector<RC::Unreal::UObject*> Instances(RC::Unreal::UClass* cls, bool includeDerived = true);
}
#endif
