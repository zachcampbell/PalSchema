#include "Utility/Logging.h"
#include "SDK/Helper/LinuxSpawn.h"
#include "SDK/Classes/Custom/UObjectGlobals.h"
#include <Unreal/UFunction.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/Rotator.hpp>
#include <cmath>
#include <cstring>
#include <vector>

using namespace RC;
using namespace RC::Unreal;

namespace UECustom
{
    namespace
    {
        struct Fn { UFunction* fn{}; int32_t size{}; };
        int32_t off(UStruct* st, const char16_t* name, bool required = true)
        {
            for (UStruct* c = st; c; c = c->GetSuperStruct())
                if (auto* p = c->FindProperty(FName(name, FNAME_Find))) return p->GetOffset_Internal();
            if (required) PS::Log<LogLevel::Error>(fmt::format(STR("Linux spawn: property {} not found on {}\n"), name, st ? st->GetName() : STR("null")));
            return -1;
        }
        void write_transform(uint8_t* dst, const FVector& Location, const FRotator& Rotation)
        {
            // Layout from the reflected /Script/CoreUObject.Transform: Rotation (Quat, 4 doubles), Translation, Scale3D (3 doubles each).
            static UScriptStruct* T = UECustom::UObjectGlobals::StaticFindObject<UScriptStruct*>(nullptr, nullptr, STR("/Script/CoreUObject.Transform"));
            static int32_t oRot = off(T, STR("Rotation")), oTr = off(T, STR("Translation")), oSc = off(T, STR("Scale3D"));
            const double d2 = M_PI / 360.0;
            double p = const_cast<FRotator&>(Rotation).GetPitch() * d2, y = const_cast<FRotator&>(Rotation).GetYaw() * d2, r = const_cast<FRotator&>(Rotation).GetRoll() * d2;
            double SP = std::sin(p), CP = std::cos(p), SY = std::sin(y), CY = std::cos(y), SR = std::sin(r), CR = std::cos(r);
            double q[4] = {CR * SP * SY - SR * CP * CY, -CR * SP * CY - SR * CP * SY, CR * CP * SY - SR * SP * CY, CR * CP * CY + SR * SP * SY};
            double t[3] = {const_cast<FVector&>(Location).X(), const_cast<FVector&>(Location).Y(), const_cast<FVector&>(Location).Z()};
            double s[3] = {1.0, 1.0, 1.0};
            std::memcpy(dst + oRot, q, sizeof q); std::memcpy(dst + oTr, t, sizeof t); std::memcpy(dst + oSc, s, sizeof s);
        }
    }

    AActor* LinuxSpawnActor(UWorld* World, UClass* Class, const FVector& Location, const FRotator& Rotation)
    {
        static UClass* GS = UECustom::UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/Engine.GameplayStatics"));
        static UFunction* Begin = UECustom::UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/Engine.GameplayStatics:BeginDeferredActorSpawnFromClass"));
        static UFunction* Finish = UECustom::UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/Engine.GameplayStatics:FinishSpawningActor"));
        static bool logged = false;
        if (!GS || !Begin || !Finish) { PS::Log<LogLevel::Error>(STR("Linux spawn: GameplayStatics functions not found\n")); return nullptr; }
        UObject* cdo = GS->GetClassDefaultObject();
        static int32_t bWC = off(Begin, STR("WorldContextObject")), bCls = off(Begin, STR("ActorClass")), bTr = off(Begin, STR("SpawnTransform")), bCol = off(Begin, STR("CollisionHandlingOverride")),
                       bOwn = off(Begin, STR("Owner")), bScale = off(Begin, STR("TransformScaleMethod"), false), bRet = off(Begin, STR("ReturnValue")),
                       fAct = off(Finish, STR("Actor")), fTr = off(Finish, STR("SpawnTransform")), fRet = off(Finish, STR("ReturnValue"));
        if (!logged)
        {
            logged = true;
            PS::Log<LogLevel::Verbose>(fmt::format(STR("Linux spawn: Begin parms {} bytes [WC {} Class {} Transform {} Collision {} Owner {} Scale {} Ret {}], Finish parms {} bytes [Actor {} Transform {} Ret {}]\n"),
                Begin->GetStructureSize(), bWC, bCls, bTr, bCol, bOwn, bScale, bRet, Finish->GetStructureSize(), fAct, fTr, fRet));
        }
        if (bWC < 0 || bCls < 0 || bTr < 0 || bRet < 0 || fAct < 0 || fTr < 0 || fRet < 0) return nullptr;
        std::vector<uint8_t> parms(static_cast<size_t>(Begin->GetStructureSize()) + 64, 0);
        *reinterpret_cast<UObject**>(parms.data() + bWC) = World;
        *reinterpret_cast<UClass**>(parms.data() + bCls) = Class;
        write_transform(parms.data() + bTr, Location, Rotation);
        parms[static_cast<size_t>(bCol)] = 1; // ESpawnActorCollisionHandlingMethod::AlwaysSpawn
        if (bScale >= 0) parms[static_cast<size_t>(bScale)] = 1; // ESpawnActorScaleMethod::MultiplyWithRoot
        cdo->ProcessEvent(Begin, parms.data());
        AActor* actor = *reinterpret_cast<AActor**>(parms.data() + bRet);
        if (!actor) { PS::Log<LogLevel::Error>(STR("Linux spawn: BeginDeferredActorSpawnFromClass returned null\n")); return nullptr; }
        std::vector<uint8_t> fparms(static_cast<size_t>(Finish->GetStructureSize()) + 64, 0);
        *reinterpret_cast<AActor**>(fparms.data() + fAct) = actor;
        write_transform(fparms.data() + fTr, Location, Rotation);
        cdo->ProcessEvent(Finish, fparms.data());
        AActor* finished = *reinterpret_cast<AActor**>(fparms.data() + fRet);
        PS::Log<LogLevel::Verbose>(fmt::format(STR("Linux spawn: {} -> begin {:#x} finish {:#x}\n"), Class->GetName(), reinterpret_cast<uintptr_t>(actor), reinterpret_cast<uintptr_t>(finished)));
        return finished ? finished : actor;
    }
}
