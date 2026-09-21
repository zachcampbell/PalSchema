#pragma once
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/AActor.hpp>
#include <Unreal/World.hpp>
#include <Unreal/Transform.hpp>

namespace UECustom
{
    // Linux: spawn an actor through GameplayStatics::BeginDeferredActorSpawnFromClass + FinishSpawningActor with the
    // parameter block laid out from the engine's reflected parameter offsets (no UE4SS param structs involved).
    RC::Unreal::AActor* LinuxSpawnActor(RC::Unreal::UWorld* World, RC::Unreal::UClass* Class, const RC::Unreal::FVector& Location, const RC::Unreal::FRotator& Rotation);
}
