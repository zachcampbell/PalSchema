#include "SDK/Classes/Custom/UBlueprintGeneratedClass.h"
#include "SDK/Classes/Custom/UInheritableComponentHandler.h"
#include "SDK/Classes/Custom/UObjectGlobals.h"
#include "SDK/Helper/Memory.h"
#include "SDK/Helper/PropertyHelper.h"
#include "SDK/PalSignatures.h"
#include "Utility/Logging.h"
#include "Unreal/CoreUObject/UObject/UnrealType.hpp"

using namespace Palworld;
using namespace RC;
using namespace RC::Unreal;

namespace UECustom {
    RC::Unreal::UClass* UBlueprintGeneratedClass::StaticClass()
    {
        static auto Class = UECustom::UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, TEXT("/Script/Engine.BlueprintGeneratedClass"));
        return Class;
    }

    void UBlueprintGeneratedClass::PurgeClass(bool bRecompilingOnLoad)
    {
        using FnSignature = void(*)(UClass*, bool);
        static FnSignature fn = nullptr;

        if (!fn)
        {
            auto staticClass = StaticClass();
            // Linux: every UE4SS/Windows vtable index after the destructor is +1 here (Itanium double destructor).
            // Index 121 (0x3c8) on the BlueprintGeneratedClass vtable is UBlueprintGeneratedClass::PurgeClass
            // (0xa15f310, 21 bytes, calls UClass::PurgeClass 0x7a44e00); 120 would be a different function.
            auto fnAddress = Palworld::GetVirtualFunctionFromClass(staticClass, 121);
            fn = reinterpret_cast<FnSignature>(fnAddress);
        }

        if (!fn)
        {
            PS::Log<LogLevel::Error>(STR("Failed to call UBlueprintGeneratedClass::PurgeClass because function address was invalid.\n"));
            return;
        }

        fn(this, bRecompilingOnLoad);
    }

    UInheritableComponentHandler* UBlueprintGeneratedClass::GetInheritableComponentHandler()
    {
        auto InheritableComponentHandler = 
            PropertyHelper::GetValuePtrByPropertyNameInChain<RC::Unreal::TObjectPtr<UInheritableComponentHandler>>(this, (STR("InheritableComponentHandler")));

        if (!InheritableComponentHandler)
        {
            return nullptr;
        }

        return (*InheritableComponentHandler).Get();
    }

    USimpleConstructionScript* UBlueprintGeneratedClass::GetSimpleConstructionScript()
    {
        auto SimpleConstructionScript =
            PropertyHelper::GetValuePtrByPropertyNameInChain<RC::Unreal::TObjectPtr<USimpleConstructionScript>>(this, (STR("SimpleConstructionScript")));

        if (!SimpleConstructionScript)
        {
            return nullptr;
        }

        return (*SimpleConstructionScript).Get();
    }
}