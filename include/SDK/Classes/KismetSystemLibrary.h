#pragma once

#include "Unreal/UObject.hpp"
#include "Unreal/SoftObjectPtr.hpp"


namespace UECustom {
	class UKismetSystemLibrary : public RC::Unreal::UObject {
	public:
        static void CollectGarbage();

		static RC::Unreal::UObject* LoadAsset_Blocking(const RC::Unreal::TSoftObjectPtr<UObject>& Asset, bool bSetRootSet = false);

		static RC::Unreal::UObject* LoadAsset_Blocking(const RC::StringType& AssetPath, bool bSetRootSet = false);

		// Resolves a soft reference that is already loaded without loading anything (engine FSoftObjectPath::ResolveObject).
		static RC::Unreal::UObject* Conv_SoftObjectReferenceToObject(const RC::Unreal::TSoftObjectPtr<UObject>& SoftObject);
	private:
		static UKismetSystemLibrary* GetDefaultObj();
	};
}