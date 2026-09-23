#ifdef __linux__
#include "SDK/Helper/LinuxObjectIndex.h"
#include "Unreal/UObjectGlobals.hpp"
#include "Utility/Logging.h"
#include <unordered_map>
#include <chrono>

using namespace RC;
using namespace RC::Unreal;

namespace Palworld::LinuxObjectIndex {
    static std::unordered_map<UClass*, std::vector<UObject*>> g_index;
    static bool g_valid = false;

    void Invalidate() { g_valid = false; g_index.clear(); }

    static void Build()
    {
        auto t0 = std::chrono::steady_clock::now();
        g_index.clear();
        UObjectGlobals::ForEachUObject([&](UObject* obj, int32, int32) {
            if (obj && !obj->HasAnyFlags(RF_ClassDefaultObject))
            {
                if (auto* cls = obj->GetClassPrivate()) g_index[cls].push_back(obj);
            }
            return LoopAction::Continue;
        });
        g_valid = true;
        PS::Log<LogLevel::Normal>(STR("Linux: object index built, {} classes, in {} ms.\n"), g_index.size(),
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count());
    }

    std::vector<UObject*> Instances(UClass* cls, bool includeDerived)
    {
        if (!g_valid) Build();
        std::vector<UObject*> out;
        if (!cls) return out;
        if (auto it = g_index.find(cls); it != g_index.end()) out = it->second;
        if (includeDerived)
        {
            for (auto& [k, v] : g_index)
            {
                if (k != cls && k->IsChildOf(cls)) out.insert(out.end(), v.begin(), v.end());
            }
        }
        return out;
    }
}
#endif
