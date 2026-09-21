// palhook (Linux port): soft object/class references are rejected before any row mutation until UE4SS's
// FSoftObjectPath matches this engine's 5.1 layout (FTopLevelAssetPath + FString; observed element size 48
// for TSoftObjectPtr against UE4SS's 32). The flag exists so the fixed layout can be enabled deliberately.
#include "SDK/Helper/PropertyHelper.h"
#include "Unreal/Property/FArrayProperty.hpp"
#include "Unreal/Property/FMapProperty.hpp"
#include "Unreal/Property/FSetProperty.hpp"
#include "Unreal/Property/FStructProperty.hpp"
#include "Unreal/UScriptStruct.hpp"
#include "Helpers/String.hpp"
#include <stdexcept>
namespace Palworld
{
    bool g_linux_soft_ref_writes_enabled = false;   // soft OBJECT references: gated on SoftObjectPath + a SoftObjectProperty
    bool g_linux_soft_class_writes_enabled = false; // soft CLASS references: gated separately on SoftClassPath + a SoftClassProperty

    static bool PropertyHoldsSoftRef(RC::Unreal::FProperty* Property, int Depth, RC::StringType& Where)
    {
        using namespace RC::Unreal;
        if (!Property || Depth > 8) return false;
        auto ClassName = Property->GetClass().GetFName().ToString();
        if (ClassName == STR("SoftObjectProperty") && !g_linux_soft_ref_writes_enabled) { Where = Property->GetName(); return true; }
        if (ClassName == STR("SoftClassProperty") && !g_linux_soft_class_writes_enabled) { Where = Property->GetName(); return true; }
        if (ClassName == STR("ArrayProperty")) return PropertyHoldsSoftRef(static_cast<FArrayProperty*>(Property)->GetInner(), Depth + 1, Where);
        if (ClassName == STR("SetProperty")) return PropertyHoldsSoftRef(static_cast<FSetProperty*>(Property)->GetElementProp(), Depth + 1, Where);
        if (ClassName == STR("MapProperty"))
        {
            auto* Map = static_cast<FMapProperty*>(Property);
            return PropertyHoldsSoftRef(Map->GetKeyProp(), Depth + 1, Where) || PropertyHoldsSoftRef(Map->GetValueProp(), Depth + 1, Where);
        }
        if (ClassName == STR("StructProperty"))
        {
            if (auto Inner = static_cast<FStructProperty*>(Property)->GetStruct())
                for (FProperty* Member : Inner->ForEachProperty())
                    if (PropertyHoldsSoftRef(Member, Depth + 1, Where)) return true;
        }
        return false;
    }

    // Throws before any write if the JSON names a property that holds a soft reference at any nesting depth.
    void LinuxPreflightRow(RC::Unreal::UScriptStruct* RowStruct, const nlohmann::json& Data)
    {
        if ((g_linux_soft_ref_writes_enabled && g_linux_soft_class_writes_enabled) || !RowStruct || !Data.is_object()) return;
        for (auto& [Key, Value] : Data.items())
        {
            auto* Property = PropertyHelper::GetPropertyByName(RowStruct, RC::to_generic_string(Key));
            RC::StringType Where;
            if (Property && PropertyHoldsSoftRef(Property, 0, Where))
                throw std::runtime_error("Linux port: writes to soft object/class references are rejected until FSoftObjectPath matches the 5.1 engine layout (property '" + Key + "' via '" + RC::to_string(Where) + "'); row left untouched");
        }
    }

    void LinuxRejectSoftClassWrite(RC::Unreal::FProperty* Property)
    {
        if (g_linux_soft_class_writes_enabled) return;
        throw std::runtime_error("Linux port: soft class reference write rejected (" + RC::to_string(Property ? Property->GetName() : RC::StringType(STR("?"))) + "); SoftClassPath layout not yet verified for this engine");
    }

    void LinuxRejectSoftRefWrite(RC::Unreal::FProperty* Property)
    {
        if (g_linux_soft_ref_writes_enabled) return;
        throw std::runtime_error("Linux port: soft object/class reference write rejected (" + RC::to_string(Property ? Property->GetName() : RC::StringType(STR("?"))) + "); FSoftObjectPath layout not yet corrected for this engine");
    }
}
