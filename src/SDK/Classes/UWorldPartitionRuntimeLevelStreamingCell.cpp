#include <Unreal/FProperty.hpp>
#include <Unreal/UClass.hpp>
#include <unordered_map>
#include "Utility/Logging.h"
#include "SDK/Classes/UWorldPartitionRuntimeLevelStreamingCell.h"
#include "Helpers/Casting.hpp"

using namespace RC;
using namespace RC::Unreal;

namespace UECustom {
    // Linux: the Windows build read these at fixed offsets (0x48, 0x138, 0xB5, 0xE8, 0x100, 0x104). They are all
    // reflected UPROPERTYs on UWorldPartitionRuntimeCell / ...LevelStreamingCell, so resolve them through the class
    // chain once and fall back to the old offset only if the name is missing (logged).
    namespace
    {
        template <typename T>
        T* cell_prop(RC::Unreal::UObject* self, const RC::StringType& name, size_t fallback)
        {
            static std::unordered_map<RC::StringType, int32_t> offsets;
            auto it = offsets.find(name);
            if (it == offsets.end())
            {
                int32_t off = -1;
                for (RC::Unreal::UStruct* c = self->GetClassPrivate(); c && off < 0; c = c->GetSuperStruct())
                    if (auto* p = c->FindProperty(RC::Unreal::FName(name, RC::Unreal::FNAME_Find))) off = p->GetOffset_Internal();
                PS::Log<LogLevel::Verbose>(STR("Linux: cell property {} -> offset {} (Windows constant {})\n"), name, off, fallback);
                if (off < 0) off = static_cast<int32_t>(fallback);
                it = offsets.emplace(name, off).first;
            }
            return reinterpret_cast<T*>(reinterpret_cast<uint8_t*>(self) + it->second);
        }
    }

    FBox& UECustom::UWorldPartitionRuntimeLevelStreamingCell::GetContentBounds()
    {
        return *cell_prop<FBox>(this, STR("ContentBounds"), 0x48);
    }

    ULevelStreaming* UWorldPartitionRuntimeLevelStreamingCell::GetLevelStreaming()
    {
        return *cell_prop<ULevelStreaming*>(this, STR("LevelStreaming"), 0x138);
    }

    bool& UWorldPartitionRuntimeLevelStreamingCell::GetIsHLOD()
    {
        return *cell_prop<bool>(this, STR("bIsHLOD"), 0xB5);
    }

    RC::Unreal::FVector& UWorldPartitionRuntimeLevelStreamingCell::GetPosition()
    {
        return *cell_prop<RC::Unreal::FVector>(this, STR("Position"), 0xE8);
    }

    float& UWorldPartitionRuntimeLevelStreamingCell::GetExtent()
    {
        return *cell_prop<float>(this, STR("Extent"), 0x100);
    }

    int& UWorldPartitionRuntimeLevelStreamingCell::GetLevel()
    {
        return *cell_prop<int>(this, STR("Level"), 0x104);
    }
}
