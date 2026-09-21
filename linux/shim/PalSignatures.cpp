// Linux SignatureManager for PalSchema: adapters over UE4SS exports and addresses proven on the shadow.
#include "SDK/PalSignatures.h"
#include "SDK/Helper/PropertyHelper.h"
#include <DynamicOutput/DynamicOutput.hpp>
#include <cstddef>
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/FField.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/Core/Containers/Array.hpp>
#include <Unreal/Core/Containers/Map.hpp>
#include <Unreal/NameTypes.hpp>
#include <Unreal/Core/Containers/FString.hpp>
#include <dlfcn.h>
#include <cstring>
#include <Unreal/FSoftObjectPath.hpp>
#include <Unreal/SoftObjectPtr.hpp>
#include <Unreal/UScriptStruct.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/CoreUObject/UObject/Class.hpp>
#include <cstdint>
#include <algorithm>
#include <initializer_list>
#include <unordered_map>
#include <string>

namespace
{
    using namespace RC::Unreal;

    bool Adapter_FField_IsA(FField* Field, FFieldClass* FieldClass)
    {
        if (!Field || !FieldClass) return false;
        return Field->IsA(FFieldClassVariant(FieldClass));
    }

    TMap<FName, FFieldClass*>* Adapter_GetNameToFieldClassMap()
    {
        return &FFieldClass::GetNameToFieldClassMap();
    }

    void* engine_vtable_slot(const char* mangled_vtable, size_t byte_offset);
    UObject* Adapter_StaticFindObject(UClass* ObjectClass, UObject* InObjectPackage, const TCHAR* OrigInName, bool bExactClass)
    {
        return UObjectGlobals::StaticFindObject_InternalSlow(ObjectClass, InObjectPackage, OrigInName, bExactClass);
    }

    // UClass::GetDefaultObject(bool bCreateIfNeeded). The engine inlines it everywhere on Linux as
    // `if (!ClassDefaultObject && bCreate) this->vtable[0x3f8](this)` (Ghidra: 390 such sites, e.g. 0x46cb68e); slot
    // 0x3f8 is UE4SS's 5.1 baseline CreateDefaultObject 0x3f0 + the Linux 8 and holds UClass::CreateDefaultObject
    // 0x7a41c10 (force-registers the super, allocates the CDO with GetDefaultObjectName, runs ClassConstructor).
    // The creating path is only enabled once the exported UClass vtable's slot and that function's prologue both
    // match; otherwise the adapter reads the field like before and PalSchema's blueprint-class path gets null.
    constexpr size_t kLinuxCreateDefaultObjectSlot = 0x3f8;
    bool g_create_cdo_enabled = false;
    void LinuxCreateDefaultObjectGate()
    {
        static const uint8_t prologue[] = {0x41, 0x57, 0x41, 0x56, 0x41, 0x54, 0x53, 0x48, 0x81, 0xec, 0x58, 0x10, 0x00, 0x00, 0x49, 0x89, 0xfc, 0x48, 0x83, 0xbf, 0x10, 0x01, 0x00, 0x00, 0x00};
        void* slot = engine_vtable_slot("_ZTV6UClass", kLinuxCreateDefaultObjectSlot);
        const bool ok = slot == reinterpret_cast<void*>(0x7a41c10) && std::memcmp(slot, prologue, sizeof prologue) == 0;
        g_create_cdo_enabled = ok;
        RC::Output::send<RC::LogLevel::Default>(STR("[PalSchema] UClass::CreateDefaultObject: UClass vtable slot 0x3f8 = {} -> {}\n"), slot, ok ? STR("verified, creating GetDefaultObject enabled") : STR("MISMATCH, GetDefaultObject stays read-only"));
    }
    UObject* Adapter_GetDefaultObject(UClass* Class, bool bCreateIfNeeded)
    {
        if (!Class) return nullptr;
        UObject* cdo = Class->GetClassDefaultObject();
        if (!cdo && bCreateIfNeeded && g_create_cdo_enabled)
        {
            auto** vtable = *reinterpret_cast<void***>(Class);
            auto fn = reinterpret_cast<UObject* (*)(UClass*)>(vtable[kLinuxCreateDefaultObjectSlot / sizeof(void*)]);
            cdo = fn(Class);
            RC::Output::send<RC::LogLevel::Default>(STR("[PalSchema] CreateDefaultObject({}) -> {}\n"), Class->GetName(), static_cast<void*>(cdo));
        }
        return cdo;
    }

    // Virtual UDataTable::Serialize(FArchive&) from the exported vtable: slot 0xD0 (5.1 baseline 0xC8 + 8), proven
    // by the palhook Serialize detour (runs 11 to 36).
    void* engine_vtable_slot(const char* mangled_vtable, size_t byte_offset)
    {
        auto* vt = static_cast<uint8_t*>(dlsym(RTLD_DEFAULT, mangled_vtable));
        if (!vt) return nullptr;
        return *reinterpret_cast<void**>(vt + 16 + byte_offset);
    }

    // FName::ToString for UE4SS's Function<void(const FName*, FStringOut&)>: the native Linux symbol is
    // `FString FName::ToString() const` (Itanium sret: rdi = out, rsi = this), so swap the arguments.
    // 0x7977420 on PalServer-Linux-Shipping v1.0.5.102999; proven from the loading thread (run 14) and used by
    // the probe mod in every run since.
    void Adapter_FName_ToString(const FName* Name, FStringOut& Out)
    {
        reinterpret_cast<void* (*)(void*, const void*)>(0x7977420)(static_cast<FString*>(&Out), Name);
    }

    // Linux soft-reference gate: PalSchema's soft object/class writers stay rejected until UE4SS's structs are
    // proven to match this engine at runtime: reflected /Script/CoreUObject.SoftObjectPath size and member
    // offsets, and the element size of a real SoftObjectProperty (PalSkinAssetMap.StaticMesh) against
    // sizeof(TSoftObjectPtr).
    void LinuxSoftRefGate()
    {
        auto* sop = static_cast<UScriptStruct*>(UObjectGlobals::StaticFindObject_InternalNoToStringFromStrings({u"/Script/CoreUObject", u"SoftObjectPath"}));
        auto* row = static_cast<UStruct*>(UObjectGlobals::StaticFindObject_InternalNoToStringFromStrings({u"/Script/Pal", u"PalSkinAssetMap"}));
        FProperty* mesh = row ? row->FindProperty(FName(STR("StaticMesh"), FNAME_Find)) : nullptr;
        int32_t asset_off = -1, sub_off = -1;
        if (sop) for (FProperty* p : sop->ForEachProperty()) { auto n = p->GetName(); if (n == STR("AssetPath")) asset_off = p->GetOffset_Internal(); else if (n == STR("SubPathString")) sub_off = p->GetOffset_Internal(); }
        const bool ok = sop && mesh && sop->GetStructureSize() == static_cast<int32_t>(sizeof(FSoftObjectPath)) && asset_off == 0 && sub_off == static_cast<int32_t>(offsetof(FSoftObjectPath, SubPathString))
                        && mesh->GetElementSize() == static_cast<int32_t>(sizeof(TSoftObjectPtr<UObject>));
        Palworld::g_linux_soft_ref_writes_enabled = ok;
        // Soft CLASS references: their own reflection (SoftClassPath) and a real SoftClassProperty
        // (PalSkinAssetMap.BaseCharacterClass) against the FSoftObjectPtr PalSchema writes for them.
        auto* scp = static_cast<UScriptStruct*>(UObjectGlobals::StaticFindObject_InternalNoToStringFromStrings({u"/Script/CoreUObject", u"SoftClassPath"}));
        FProperty* cls = row ? row->FindProperty(FName(STR("BaseCharacterClass"), FNAME_Find)) : nullptr;
        int32_t c_asset_off = -1, c_sub_off = -1;
        // SoftClassPath declares no members of its own: it derives from SoftObjectPath. Walk the super chain.
        for (UStruct* st = scp; st; st = st->GetSuperStruct())
            for (FProperty* p : st->ForEachProperty()) { auto n = p->GetName(); if (n == STR("AssetPath")) c_asset_off = p->GetOffset_Internal(); else if (n == STR("SubPathString")) c_sub_off = p->GetOffset_Internal(); }
        const bool cls_ok = scp && cls && scp->GetStructureSize() == static_cast<int32_t>(sizeof(FSoftObjectPath)) && c_asset_off == 0 && c_sub_off == static_cast<int32_t>(offsetof(FSoftObjectPath, SubPathString))
                            && cls->GetElementSize() == static_cast<int32_t>(sizeof(FSoftObjectPtr)) && cls->GetClass().GetName() == STR("SoftClassProperty");
        Palworld::g_linux_soft_class_writes_enabled = cls_ok;
        RC::Output::send<RC::LogLevel::Default>(STR("[PalSchema] soft-class gate: engine SoftClassPath size {} (UE4SS {}), AssetPath@{} SubPathString@{}, SoftClassProperty element {} (UE4SS FSoftObjectPtr {}) -> writes {}\n"),
            scp ? scp->GetStructureSize() : -1, static_cast<int>(sizeof(FSoftObjectPath)), c_asset_off, c_sub_off, cls ? cls->GetElementSize() : -1, static_cast<int>(sizeof(FSoftObjectPtr)), cls_ok ? STR("ENABLED") : STR("REJECTED"));
        RC::Output::send<RC::LogLevel::Default>(STR("[PalSchema] soft-reference gate: engine SoftObjectPath size {} (UE4SS {}), AssetPath@{} SubPathString@{} (UE4SS @{}), SoftObjectProperty element {} (UE4SS TSoftObjectPtr {}) -> writes {}\n"),
            sop ? sop->GetStructureSize() : -1, static_cast<int>(sizeof(FSoftObjectPath)), asset_off, sub_off, static_cast<int>(offsetof(FSoftObjectPath, SubPathString)),
            mesh ? mesh->GetElementSize() : -1, static_cast<int>(sizeof(TSoftObjectPtr<UObject>)), ok ? STR("ENABLED") : STR("REJECTED"));
    }

    // StaticConstructObject_Internal(const FStaticConstructObjectParameters&) on PalServer-Linux-Shipping
    // v1.0.5.102999: 0x7bba630, derived from the GameplayStatics::SpawnObject native (exec thunk 0xa407c50 ->
    // impl 0xa407d30 -> params ctor 0x7bbc3b0 -> call 0x7bba630; loads Class at +0 and Outer at +8). The port's
    // AOB scan picks a mid-function address (port patch 9 rejects it) and its UE4SS_Addresses.ini path throws
    // for any absent key, which is lethal on this build, so the mod installs the address itself, after checking
    // the prologue bytes so a game update cannot silently point it at something else.
    void LinuxStaticConstructObject()
    {
        static const uint8_t prologue[] = {0x55, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x53, 0x48, 0x81, 0xec, 0x28, 0x02, 0x00, 0x00, 0x48, 0x89, 0xfb, 0x4c, 0x8b, 0x37, 0x4c, 0x8b, 0x7f, 0x08};
        auto* fn = reinterpret_cast<const uint8_t*>(0x7bba630);
        const bool ok = std::memcmp(fn, prologue, sizeof prologue) == 0;
        if (ok) UObjectGlobals::SetupStaticConstructObjectInternalAddress(const_cast<uint8_t*>(fn));
        RC::Output::send<RC::LogLevel::Default>(STR("[PalSchema] StaticConstructObject_Internal at {}: prologue {} -> {}\n"), static_cast<const void*>(fn), ok ? STR("verified") : STR("MISMATCH"), ok ? STR("installed") : STR("left unset (object construction unavailable)"));
    }

    // Engine functions located with Ghidra 12.1.3 on PalServer-Linux-Shipping v1.0.5.102999 (notes in
    // /mnt/FTY/palhook/notes/ghidra-signatures.md). Each address is accepted only if the first bytes still match
    // the prologue recorded at location time, so a game update fails loud instead of pointing PalSchema at
    // whatever now lives there. All four are plain SysV functions (this in rdi, bool in esi), so PalSchema's own
    // `void(*)(UClass*, bool)` style casts call them directly; no adapters.
    struct LinuxEngineFunction { const char* name; uintptr_t address; std::initializer_list<uint8_t> prologue; };
    void RegisterLinuxEngineFunctions(std::unordered_map<std::string, void*>& map)
    {
        static const LinuxEngineFunction fns[] = {
            // UWorld::CleanupWorld(bool bSessionEnded, bool bCleanupResources, UWorld* NewWorld): increments the static
            // cleanup tag then calls CleanupWorldInternal (0xab23e50), which compares the tag with UWorld+0x70c
            // (MemberVariableLayout CleanupWorldTag) and recurses into sub-level worlds.
            {"UWorld::CleanupWorld", 0xab1e020, {0x53, 0x83, 0x05, 0x60, 0x0f, 0x7c, 0x01, 0x01, 0x48, 0x89, 0xfb, 0x31, 0xc0, 0x48, 0x39, 0xf9}},
            // UStruct::StaticLink(bool bRelinkExistingProperties): FArchive dummy on the stack (vtable 0x1a3bbb8), virtual
            // Link at slot 0x2d0 (5.1 baseline 0x2c8 + the Linux 8), FArchive destroyed. 31 callers (StaticClass registration).
            {"UStruct::StaticLink", 0x7a39e60, {0x55, 0x41, 0x56, 0x53, 0x48, 0x81, 0xec, 0xa0, 0x00, 0x00, 0x00, 0x89, 0xf5, 0x48, 0x89, 0xfb}},
            // UClass::AssembleReferenceTokenStream(bool bForce): the only caller of the cold stub that logs
            // "AssembleReferenceTokenStream for %s called on a non-game thread"; tests CLASS_TokenStreamAssembled (0x400000)
            // in ClassFlags at +0xd4 and clears it when bForce.
            {"UClass::AssembleReferenceTokenStream", 0x7ad40f0, {0x55, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x53, 0x48, 0x83, 0xec, 0x58, 0x89, 0xf5}},
            // FPakPlatformFile::GetPakFolders(const TCHAR* CmdLine, TArray<FString>& OutPakFolders): the one .text user of
            // the UTF-16 "%sPaks/" format, appends the three pak roots. Runs during pak mounting at boot, before any mod
            // starts, so on this build the hook can only catch later mounts.
            {"FPakPlatformFile::GetPakFolders", 0x9dfb450, {0x41, 0x56, 0x53, 0x48, 0x83, 0xec, 0x28, 0x49, 0x89, 0xf6, 0x48, 0x8d, 0x7c, 0x24, 0x18, 0xe8}},
        };
        for (const auto& f : fns)
        {
            const auto* code = reinterpret_cast<const uint8_t*>(f.address);
            const bool ok = std::equal(f.prologue.begin(), f.prologue.end(), code);
            if (ok) map[f.name] = reinterpret_cast<void*>(f.address);
            RC::Output::send<RC::LogLevel::Default>(STR("[PalSchema] {} at {}: prologue {}\n"), RC::to_generic_string(f.name), static_cast<const void*>(code), ok ? STR("verified") : STR("MISMATCH, signature withheld"));
        }
    }

    void Adapter_GetObjectsOfClass(const UClass* ClassToLookFor, TArray<UObject*>& Results, bool bIncludeDerivedClasses,
                                   EObjectFlags ExcludeFlags, EInternalObjectFlags)
    {
        UObjectGlobals::ForEachUObject([&](UObject* Object, int32, int32) {
            if (!Object) return RC::LoopAction::Continue;
            UClass* Cls = Object->GetClassPrivate();
            bool match = bIncludeDerivedClasses ? Object->IsA(const_cast<UClass*>(ClassToLookFor)) : (Cls == ClassToLookFor);
            if (match && !Object->HasAnyFlags(ExcludeFlags)) Results.Add(Object);
            return RC::LoopAction::Continue;
        });
    }
}

namespace Palworld
{
    void SignatureManager::Initialize()
    {
        SignatureMap["FField::IsA"] = reinterpret_cast<void*>(&Adapter_FField_IsA);
        SignatureMap["FFieldClass::GetNameToFieldClassMap"] = reinterpret_cast<void*>(&Adapter_GetNameToFieldClassMap);
        SignatureMap["UObjectGlobals::StaticFindObject"] = reinterpret_cast<void*>(&Adapter_StaticFindObject);
        SignatureMap["GetObjectsOfClass"] = reinterpret_cast<void*>(&Adapter_GetObjectsOfClass);
        // Engine addresses proven on PalServer-Linux-Shipping v1.0.5.102999 (see the inventory).
        SignatureMap["FName::ToString_Wchar"] = reinterpret_cast<void*>(&Adapter_FName_ToString); // sret-swapping adapter over 0x7977420
        SignatureMap["FMemory::Free"] = reinterpret_cast<void*>(0x78118b0);
        SignatureMap["UClass::GetDefaultObject"] = reinterpret_cast<void*>(&Adapter_GetDefaultObject);
        LinuxSoftRefGate();
        LinuxStaticConstructObject();
        LinuxCreateDefaultObjectGate();
        SignatureMap["UDataTable::Serialize"] = engine_vtable_slot("_ZTV10UDataTable", 0xD0);
        RegisterLinuxEngineFunctions(SignatureMap);
        // Deliberately absent (GetSignature returns nullptr and the loaders log and skip): FName::Constructor
        // (UE4SS's own is kept), UBlueprintGeneratedClass::FindComponentTemplateByName, AsyncTask, and the five
        // item save-safety hooks. See notes/palschema-signature-inventory.md for what each needs.
    }

    void* SignatureManager::GetSignature(const std::string& ClassAndFunction)
    {
        if (SignatureMap.empty()) Initialize();
        auto it = SignatureMap.find(ClassAndFunction);
        return it == SignatureMap.end() ? nullptr : it->second;
    }
}
