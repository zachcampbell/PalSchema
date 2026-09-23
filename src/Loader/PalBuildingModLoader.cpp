#include "Unreal/UObjectGlobals.hpp"
#include <fmt/format.h>
#include <fmt/xchar.h>
#include "Unreal/UScriptStruct.hpp"
#include "Unreal/FProperty.hpp"
#include "Unreal/Engine/UDataTable.hpp"
#include "Helpers/String.hpp"
#include "SDK/Helper/PropertyHelper.h"
#include "SDK/Structs/Custom/FScriptMapHelper.h"
#include "SDK/Classes/Custom/UObjectGlobals.h"
#include "Unreal/CoreUObject/UObject/UnrealType.hpp"
#include <cstring>
#include "Utility/Logging.h"
#include "Utility/JsonHelpers.h"
#include "Loader/PalBuildingModLoader.h"
#include <chrono>
#include "SDK/Helper/LinuxObjectIndex.h"

using namespace RC;
using namespace RC::Unreal;

namespace Palworld {
	PalBuildingModLoader::PalBuildingModLoader() : PalModLoaderBase("buildings") {
        SetDisplayName(TEXT("Building Mod Loader"));
    }

	PalBuildingModLoader::~PalBuildingModLoader() {}

    void PalBuildingModLoader::OnLoad(const std::filesystem::path& loaderPath, const RC::StringType& modName, const EEngineLifecyclePhase& engineLifecyclePhase)
    {
        if (engineLifecyclePhase != EEngineLifecyclePhase::GameInstanceInit)
        {
            return;
        }

        PS::JsonHelpers::ParseJsonFilesInPath(loaderPath, [&](const nlohmann::json& data) {
            LoadBuildings(data);
        });
    }

    void PalBuildingModLoader::OnAutoReload(const RC::StringType& modName, const std::filesystem::path& modFilePath)
    {
        PS::JsonHelpers::ParseJsonFileInPath(modFilePath, [&](const nlohmann::json& data) {
            LoadBuildings(data);
        });
    }

    bool PalBuildingModLoader::CanInitialize(const EEngineLifecyclePhase& engineLifecyclePhase)
    {
        if (engineLifecyclePhase == EEngineLifecyclePhase::GameInstanceInit)
        {
            return true;
        }

        return false;
    }

    bool PalBuildingModLoader::OnInitialize()
    {
        try
        {
            m_mapObjectAssignData = GetDatatableByName("DT_MapObjectAssignData");
            m_mapObjectFarmCrop = GetDatatableByName("DT_MapObjectFarmCrop");
            m_mapObjectItemProductDataTable = GetDatatableByName("DT_MapObjectItemProductDataTable");
            m_mapObjectMasterDataTable = GetDatatableByName("DT_MapObjectMasterDataTable");
            m_mapObjectNameTable = GetDatatableByName("DT_MapObjectNameText");
            m_buildObjectDataTable = GetDatatableByName("DT_BuildObjectDataTable");
            m_buildObjectIconDataTable = GetDatatableByName("DT_BuildObjectIconDataTable");
            m_buildObjectDescTable = GetDatatableByName("DT_BuildObjectDescText");
            m_technologyRecipeUnlockTable = GetDatatableByName("DT_TechnologyRecipeUnlock");
            m_technologyNameTable = GetDatatableByName("DT_TechnologyNameText");
            m_technologyDescTable = GetDatatableByName("DT_TechnologyDescText");
        }
        catch (const std::exception& e)
        {
            PS::Log<LogLevel::Error>(STR("Unable to initialize {}, {}\n"), GetDisplayName(), RC::to_generic_string(e.what()));
            return false;
        }

        return true;
    }

    void PalBuildingModLoader::LoadBuildings(const nlohmann::json& data)
    {
        for (auto& [Key, Properties] : data.items())
        {
            auto BuildingId = FName(RC::to_generic_string(Key), FNAME_Add);
            auto TableRow = m_mapObjectMasterDataTable->FindRowUnchecked(BuildingId);
            if (TableRow)
            {
                PS::Log<LogLevel::Error>(STR("Editing of buildings should be done via raw tables instead\n"));
            }
            else
            {
                Add(BuildingId, Properties);
            }
        }
    }

	void PalBuildingModLoader::Add(const RC::Unreal::FName& BuildingId, const nlohmann::json& Data)
	{
		if (!Data.contains("BlueprintClassName"))
		{
			throw std::runtime_error(RC::fmt("%S is missing the property 'BlueprintClassName'", BuildingId.ToString().c_str()));
		}

		if (!Data.contains("BlueprintClassSoft"))
		{
			throw std::runtime_error(RC::fmt("%S is missing the property 'BlueprintClassSoft'", BuildingId.ToString().c_str()));
		}

		if (!Data.contains("IconTexture"))
		{
			throw std::runtime_error(RC::fmt("%S is missing the property 'IconTexture'", BuildingId.ToString().c_str()));
		}

        auto TableRow = m_mapObjectMasterDataTable->FindRowUnchecked(BuildingId);
        auto TableRowStruct = m_mapObjectMasterDataTable->GetRowStruct().Get();
        if (TableRow)
        {
            try
            {
                for (FProperty* Property : TFieldRange<FProperty>(TableRowStruct, EFieldIterationFlags::IncludeSuper))
                {
                    auto PropertyName = RC::to_string(Property->GetName());

                    if (PropertyName == "Editor_RowNameHash")
                    {
                        continue;
                    }

                    if (Data.contains(PropertyName))
                    {
                        PropertyHelper::CopyJsonValueToContainer(TableRow, Property, Data.at(PropertyName));
                    }
                }
            }
            catch (const std::exception& e)
            {
                PS::Log<LogLevel::Error>(STR("Failed to modify Row '{}' in {}: {}\n"), BuildingId.ToString(), m_mapObjectMasterDataTable->GetFullName(), RC::to_generic_string(e.what()));
            }
        }
        else
        {
            auto RowData = FMemory::Malloc(TableRowStruct->GetStructureSize());
            TableRowStruct->InitializeStruct(RowData);
            try
            {
                for (FProperty* Property : TFieldRange<FProperty>(TableRowStruct, EFieldIterationFlags::IncludeSuper))
                {
                    auto PropertyName = RC::to_string(Property->GetName());

                    if (PropertyName == "Editor_RowNameHash")
                    {
                        continue;
                    }

                    if (Data.contains(PropertyName))
                    {
                        PropertyHelper::CopyJsonValueToContainer(RowData, Property, Data.at(PropertyName));
                    }
                }

                m_mapObjectMasterDataTable->AddRow(BuildingId, *static_cast<RC::Unreal::FTableRowBase*>(RowData));

                PS::Log<LogLevel::Normal>(STR("Added building '{}'\n"), BuildingId.ToString());
            }
            catch (const std::exception& e)
            {
                FMemory::Free(RowData);
                PS::Log<LogLevel::Error>(STR("Failed to add Row '{}' to {}: {}\n"), BuildingId.ToString(), m_mapObjectMasterDataTable->GetFullName(), RC::to_generic_string(e.what()));
            }
        }

		{ auto _t = std::chrono::steady_clock::now(); SetupIconData(BuildingId, Data); auto _ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - _t).count(); if (_ms >= 50) PS::Log<LogLevel::Verbose>(STR("[timing] building {} SetupIconData {} ms\n"), BuildingId.ToString(), _ms); }

        { auto _t = std::chrono::steady_clock::now(); SetupTranslations(BuildingId, Data); auto _ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - _t).count(); if (_ms >= 50) PS::Log<LogLevel::Verbose>(STR("[timing] building {} SetupTranslations {} ms\n"), BuildingId.ToString(), _ms); }

		if (Data.contains("BuildingData"))
		{
			{ auto _t = std::chrono::steady_clock::now(); SetupBuildData(BuildingId, Data.at("BuildingData")); auto _ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - _t).count(); if (_ms >= 50) PS::Log<LogLevel::Verbose>(STR("[timing] building {} SetupBuildData {} ms\n"), BuildingId.ToString(), _ms); }
		}

		if (Data.contains("Assignments"))
		{
            { auto _t = std::chrono::steady_clock::now(); SetupAssignments(BuildingId, Data.at("Assignments")); auto _ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - _t).count(); if (_ms >= 50) PS::Log<LogLevel::Verbose>(STR("[timing] building {} SetupAssignments {} ms\n"), BuildingId.ToString(), _ms); }
		}

        if (Data.contains("Technology"))
        {
            { auto _t = std::chrono::steady_clock::now(); SetupTechnologyData(BuildingId, Data.at("Technology")); auto _ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - _t).count(); if (_ms >= 50) PS::Log<LogLevel::Verbose>(STR("[timing] building {} SetupTechnologyData {} ms\n"), BuildingId.ToString(), _ms); }
        }

		if (Data.contains("Crop"))
		{
			SetupCropData(BuildingId, Data.at("Crop"));
		}

		if (Data.contains("ItemProductData"))
		{
			SetupItemProductData(BuildingId, Data.at("ItemProductData"));
		}
	}

	void PalBuildingModLoader::Edit(uint8_t* ExistingRow, const RC::Unreal::FName& BuildingId, const nlohmann::json& Data)
	{

	}

	void PalBuildingModLoader::SetupBuildData(const RC::Unreal::FName& BuildingId, const nlohmann::json& Data)
	{
        auto TableRow = m_buildObjectDataTable->FindRowUnchecked(BuildingId);
        auto TableRowStruct = m_buildObjectDataTable->GetRowStruct().Get();
        if (TableRow)
        {
            try
            {
                for (FProperty* Property : TFieldRange<FProperty>(TableRowStruct, EFieldIterationFlags::IncludeSuper))
                {
                    auto PropertyName = RC::to_string(Property->GetName());

                    if (PropertyName == "RedialIndex")
                    {
                        // Deny editing of RedialIndex, if you see this comment, use raw tables instead if you really need to edit it.
                        continue;
                    }

                    if (Data.contains(PropertyName))
                    {
                        PropertyHelper::CopyJsonValueToContainer(TableRow, Property, Data.at(PropertyName));
                    }
                }
#ifdef __linux__
                LinuxRegisterLiveBuildObject(BuildingId, TableRow, TableRowStruct);
#endif
            }
            catch (const std::exception& e)
            {
                PS::Log<LogLevel::Error>(STR("Failed to modify Row '{}' in {}: {}\n"), BuildingId.ToString(), m_buildObjectDataTable->GetFullName(), RC::to_generic_string(e.what()));
            }
        }
        else
        {
            auto RowData = FMemory::Malloc(TableRowStruct->GetStructureSize());
            TableRowStruct->InitializeStruct(RowData);

            if (Data.contains("RedialIndex"))
            {
                PS::Log<LogLevel::Warning>(STR("When adding new buildings, including 'RedialIndex' will not do anything as this will be handled by PalSchema to avoid collisions. This is a warning and will not affect the loading of your building '{}'.\n"), BuildingId.ToString());
            }

            try
            {
                for (FProperty* Property : TFieldRange<FProperty>(TableRowStruct, EFieldIterationFlags::IncludeSuper))
                {
                    auto PropertyName = RC::to_string(Property->GetName());

                    // Let PalSchema handle Radial Index to avoid collisions with the build wheel in-game.
                    if (PropertyName == "RedialIndex")
                    {
                        auto RadialIndex = GetNextRadialIndex();
                        FMemory::Memcpy(Property->ContainerPtrToValuePtr<void>(RowData), &RadialIndex, sizeof(int));
                        continue;
                    }

                    if (PropertyName == "MapObjectId")
                    {
                        FMemory::Memcpy(Property->ContainerPtrToValuePtr<void>(RowData), &BuildingId, sizeof(FName));
                        continue;
                    }

                    if (Data.contains(PropertyName))
                    {
                        PropertyHelper::CopyJsonValueToContainer(RowData, Property, Data.at(PropertyName));
                    }
                }

                m_buildObjectDataTable->AddRow(BuildingId, *static_cast<RC::Unreal::FTableRowBase*>(RowData));
#ifdef __linux__
                LinuxRegisterLiveBuildObject(BuildingId, RowData, TableRowStruct);
#endif
            }
            catch (const std::exception& e)
            {
                FMemory::Free(RowData);
                PS::Log<LogLevel::Error>(STR("Failed to add Row '{}' to {}: {}\n"), BuildingId.ToString(), m_buildObjectDataTable->GetFullName(), RC::to_generic_string(e.what()));
            }
        }
	}


#ifdef __linux__
    // palhook: on a dedicated server the world's PalBuildOperator built its UPalBuildObjectDataMap from
    // DT_BuildObjectDataTable at world start, before PalSchema's rows landed (PalSchema initializes ~35 s after
    // launch here, after the map has loaded). UPalMapObjectManager's build handler looks the request's MapObjectId
    // up in that map's BuildMapObjectIds set and answers FailedPlayerCannotSpawn for anything missing, so every
    // PalSchema building was unplaceable (Diagonal Buildables, run 150). Land each new row in every live data map:
    // the id into the set, and a deep copy of the row into BuildObjectDataIdMap (its value type is the row struct).
    void PalBuildingModLoader::LinuxRegisterLiveBuildObject(const FName& BuildingId, void* RowData, UScriptStruct* RowStruct)
    {
        PS::Log<LogLevel::Verbose>(STR("Linux: live registration for build object '{}'.\n"), BuildingId.ToString());
        static auto DataMapClass = UECustom::UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/Pal.PalBuildObjectDataMap"));
        if (!DataMapClass)
        {
            static bool warned = false;
            if (!warned) { warned = true; PS::Log<LogLevel::Warning>(STR("Linux: PalBuildObjectDataMap class not found; new buildings will not be placeable until restart.\n")); }
            return;
        }
        auto IdMapProp = PropertyHelper::CastProperty<FMapProperty>(PropertyHelper::GetPropertyByName(DataMapClass, STR("BuildObjectDataIdMap")));
        // FSetProperty's static class is not resolvable through the cast on this port; match the class name instead.
        auto IdSetPropRaw = PropertyHelper::GetPropertyByName(DataMapClass, STR("BuildMapObjectIds"));
        auto IdSetProp = (IdSetPropRaw && IdSetPropRaw->GetClass().GetName() == STR("SetProperty")) ? static_cast<FSetProperty*>(IdSetPropRaw) : nullptr;
        if (!IdMapProp || !IdSetProp)
        {
            PS::Log<LogLevel::Warning>(STR("Linux: PalBuildObjectDataMap layout changed (map {} set {}); skipping live registration of '{}'.\n"),
                static_cast<void*>(IdMapProp), static_cast<void*>(IdSetProp), BuildingId.ToString());
            return;
        }
        auto KeyProp = IdMapProp->GetKeyProp();
        auto ValueProp = IdMapProp->GetValueProp();
        auto ValueStructProp = PropertyHelper::CastProperty<FStructProperty>(ValueProp);
        const bool copyValue = ValueStructProp && ValueStructProp->GetStruct() == RowStruct;
        if (!copyValue)
        {
            static bool warned = false;
            if (!warned) { warned = true; PS::Log<LogLevel::Warning>(STR("Linux: BuildObjectDataIdMap value type is not the DT_BuildObjectDataTable row struct; registering ids only.\n")); }
        }
        auto MapLayout = FScriptMap::GetScriptLayout(KeyProp->GetSize(), KeyProp->GetMinAlignment(), ValueProp->GetSize(), ValueProp->GetMinAlignment());
        auto SetLayout = FScriptSet::GetScriptLayout(sizeof(FName), alignof(FName));
        auto ElemProp = IdSetProp->GetElementProp();
        FName Id = BuildingId;
        int32 count = 0;
        auto tReg0 = std::chrono::steady_clock::now();
        for (auto* obj : Palworld::LinuxObjectIndex::Instances(DataMapClass))
        {
            auto base = reinterpret_cast<uint8*>(obj);
            auto Set = reinterpret_cast<FScriptSet*>(base + IdSetProp->GetOffset_Internal());
            // Hash and equality come from the engine's own FNameProperty so the game's lookups find the element.
            Set->Add(&Id, SetLayout,
                [&](const void* e) { return ElemProp->GetValueTypeHash(e); },
                [&](const void* a, const void* b) { return ElemProp->Identical(a, b); },
                [&](void* dst) { std::memcpy(dst, &Id, sizeof(FName)); },
                [](void*) {});
            if (copyValue)
            {
                auto Map = reinterpret_cast<FScriptMap*>(base + IdMapProp->GetOffset_Internal());
                UECustom::FScriptMapHelper Helper(Map, MapLayout, KeyProp, ValueProp);
                UECustom::FManagedValue Pair;
                Helper.InitializePair(Pair);
                std::memcpy(Pair.GetData(), &Id, sizeof(FName));
                ValueProp->CopySingleValue(static_cast<uint8*>(Pair.GetData()) + MapLayout.ValueOffset, RowData);
                Helper.Add(Pair);
                Map->Rehash(MapLayout, [&](const void* Src) { return KeyProp->GetValueTypeHash(Src); });
            }
            ++count;
        }
        PS::Log<LogLevel::Verbose>(STR("Linux: registered build object '{}' in {} live build data map(s){} in {} ms.\n"), BuildingId.ToString(), count, copyValue ? STR("") : STR(" (ids only)"),
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - tReg0).count());
    }
#endif

	void PalBuildingModLoader::SetupIconData(const RC::Unreal::FName& BuildingId, const nlohmann::json& Data)
	{
        if (Data.contains("IconTexture"))
        {
            auto TableRow = m_buildObjectIconDataTable->FindRowUnchecked(BuildingId);
            auto TableRowStruct = m_buildObjectIconDataTable->GetRowStruct().Get();
            if (TableRow)
            {
                try
                {
                    auto Property = TableRowStruct->GetPropertyByNameInChain(STR("SoftIcon"));
                    if (Property)
                    {
                        PropertyHelper::CopyJsonValueToContainer(TableRow, Property, Data.at("IconTexture"));
                    }
                }
                catch (const std::exception& e)
                {
                    PS::Log<LogLevel::Error>(STR("Failed to modify Icon '{}' in {}: {}\n"), BuildingId.ToString(), m_buildObjectIconDataTable->GetFullName(), RC::to_generic_string(e.what()));
                }
            }
            else
            {
                auto RowData = FMemory::Malloc(TableRowStruct->GetStructureSize());
                TableRowStruct->InitializeStruct(RowData);
                try
                {
                    auto Property = TableRowStruct->GetPropertyByNameInChain(STR("SoftIcon"));
                    if (Property)
                    {
                        PropertyHelper::CopyJsonValueToContainer(RowData, Property, Data.at("IconTexture"));
                        m_buildObjectIconDataTable->AddRow(BuildingId, *reinterpret_cast<RC::Unreal::FTableRowBase*>(RowData));
                    }
                }
                catch (const std::exception& e)
                {
                    PS::Log<LogLevel::Error>(STR("Failed to add Icon for '{}' in {}: {}\n"), BuildingId.ToString(), m_buildObjectIconDataTable->GetFullName(), RC::to_generic_string(e.what()));
                }

                TableRowStruct->DestroyStruct(RowData);
                FMemory::Free(RowData);
            }
        }
	}

	void PalBuildingModLoader::SetupAssignments(const RC::Unreal::FName& BuildingId, const nlohmann::json& Data)
	{
        if (!Data.is_array())
        {
            PS::Log<LogLevel::Error>(STR("Field 'Assignments' in {} must be an array of objects.\n"), BuildingId.ToString());
            return;
        }

        for (auto& Assignment : Data)
        {
            SetupAssignment(BuildingId, Assignment);
        }
	}

    void PalBuildingModLoader::SetupAssignment(const RC::Unreal::FName& BuildingId, const nlohmann::json& Data)
    {
        if (!Data.contains("WorkSuitability"))
        {
            throw std::runtime_error(RC::fmt("Assignment in Row '%S' must contain a WorkSuitability field", BuildingId.ToString().c_str()));
        }

        if (!Data.contains("WorkType"))
        {
            throw std::runtime_error(RC::fmt("Assignment in Row '%S' must contain a WorkType field", BuildingId.ToString().c_str()));
        }

        if (!Data.contains("WorkActionType"))
        {
            throw std::runtime_error(RC::fmt("Assignment in Row '%S' must contain a WorkActionType field", BuildingId.ToString().c_str()));
        }

        auto TableRowStruct = m_mapObjectAssignData->GetRowStruct().Get();
        auto RowData = FMemory::Malloc(TableRowStruct->GetStructureSize());
        TableRowStruct->InitializeStruct(RowData);

        for (FProperty* Property : TFieldRange<FProperty>(TableRowStruct, EFieldIterationFlags::IncludeSuper))
        {
            auto PropertyName = RC::to_string(Property->GetName());
            if (Data.contains(PropertyName))
            {
                PropertyHelper::CopyJsonValueToContainer(RowData, Property, Data.at(PropertyName));
            }
        }

        auto Suffix = GetAssignIDSuffixByWorkType(Data.at("WorkType"));
        auto RowFixedName = fmt::format(STR("{}{}"), BuildingId.ToString(), Suffix);
        m_mapObjectAssignData->AddRow(FName(RowFixedName, FNAME_Add), *reinterpret_cast<RC::Unreal::FTableRowBase*>(RowData));
    }

	void PalBuildingModLoader::SetupCropData(const RC::Unreal::FName& BuildingId, const nlohmann::json& Data)
	{
        auto TableRow = m_mapObjectFarmCrop->FindRowUnchecked(BuildingId);
        auto TableRowStruct = m_mapObjectFarmCrop->GetRowStruct().Get();
        if (TableRow)
        {
            try
            {
                for (FProperty* Property : TFieldRange<FProperty>(TableRowStruct, EFieldIterationFlags::IncludeSuper))
                {
                    auto PropertyName = RC::to_string(Property->GetName());
                    if (Data.contains(PropertyName))
                    {
                        PropertyHelper::CopyJsonValueToContainer(TableRow, Property, Data.at(PropertyName));
                    }
                }
            }
            catch (const std::exception& e)
            {
                PS::Log<LogLevel::Error>(STR("Failed to modify Row '{}' in {}: {}\n"), BuildingId.ToString(), m_mapObjectFarmCrop->GetFullName(), RC::to_generic_string(e.what()));
            }
        }
        else
        {
            auto RowData = FMemory::Malloc(TableRowStruct->GetStructureSize());
            TableRowStruct->InitializeStruct(RowData);

            try
            {
                for (FProperty* Property : TFieldRange<FProperty>(TableRowStruct, EFieldIterationFlags::IncludeSuper))
                {
                    auto PropertyName = RC::to_string(Property->GetName());
                    if (Data.contains(PropertyName))
                    {
                        PropertyHelper::CopyJsonValueToContainer(RowData, Property, Data.at(PropertyName));
                    }
                }

                if (Data.contains("CropItemId"))
                {
                    auto CropItemId = Data.at("CropItemId").get<std::string>();
                    m_mapObjectFarmCrop->AddRow(FName(RC::to_generic_string(CropItemId), FNAME_Add), *static_cast<RC::Unreal::FTableRowBase*>(RowData));
                }
            }
            catch (const std::exception& e)
            {
                FMemory::Free(RowData);
                PS::Log<LogLevel::Error>(STR("Failed to add Row '{}' to {}: {}\n"), BuildingId.ToString(), m_mapObjectFarmCrop->GetFullName(), RC::to_generic_string(e.what()));
            }
        }
	}

	void PalBuildingModLoader::SetupItemProductData(const RC::Unreal::FName& BuildingId, const nlohmann::json& Data)
	{
        ImportJson(BuildingId, Data, m_mapObjectItemProductDataTable);
	}

    void PalBuildingModLoader::SetupTechnologyData(const RC::Unreal::FName& BuildingId, const nlohmann::json& Data)
    {
        auto TableRow = m_technologyRecipeUnlockTable->FindRowUnchecked(BuildingId);
        auto TableRowStruct = m_technologyRecipeUnlockTable->GetRowStruct().Get();
        if (TableRow)
        {
            try
            {
                for (FProperty* Property : TFieldRange<FProperty>(TableRowStruct, EFieldIterationFlags::IncludeSuper))
                {
                    auto PropertyName = RC::to_string(Property->GetName());
                    if (PropertyName == "Name" || PropertyName == "Description")
                    {
                        continue;
                    }

                    if (Data.contains(PropertyName))
                    {
                        PropertyHelper::CopyJsonValueToContainer(TableRow, Property, Data.at(PropertyName));
                    }
                }
            }
            catch (const std::exception& e)
            {
                PS::Log<LogLevel::Error>(STR("Failed to modify Row '{}' in {}: {}\n"), BuildingId.ToString(), m_technologyRecipeUnlockTable->GetFullName(), RC::to_generic_string(e.what()));
            }
        }
        else
        {
            auto RowData = FMemory::Malloc(TableRowStruct->GetStructureSize());
            TableRowStruct->InitializeStruct(RowData);

            try
            {
                for (FProperty* Property : TFieldRange<FProperty>(TableRowStruct, EFieldIterationFlags::IncludeSuper))
                {
                    auto PropertyName = RC::to_string(Property->GetName());

                    if (PropertyName == "Name")
                    {
                        auto TechnologyName = fmt::format(STR("NAME_RECIPE_{}"), BuildingId.ToString());
                        auto TechnologyRowName = FName(TechnologyName, FNAME_Add);
                        FMemory::Memcpy(Property->ContainerPtrToValuePtr<void>(RowData), &TechnologyRowName, sizeof(FName));
                        continue;
                    }

                    if (PropertyName == "Description")
                    {
                        auto TechnologyDescription = fmt::format(STR("DESC_RECIPE_{}"), BuildingId.ToString());
                        auto TechnologyRowDescription = FName(TechnologyDescription, FNAME_Add);
                        FMemory::Memcpy(Property->ContainerPtrToValuePtr<void>(RowData), &TechnologyRowDescription, sizeof(FName));
                        continue;
                    }

                    if (Data.contains(PropertyName))
                    {
                        PropertyHelper::CopyJsonValueToContainer(RowData, Property, Data.at(PropertyName));
                    }
                }

                m_technologyRecipeUnlockTable->AddRow(BuildingId, *static_cast<RC::Unreal::FTableRowBase*>(RowData));
            }
            catch (const std::exception& e)
            {
                FMemory::Free(RowData);
                PS::Log<LogLevel::Error>(STR("Failed to add Row '{}' to {}: {}\n"), BuildingId.ToString(), m_technologyRecipeUnlockTable->GetFullName(), RC::to_generic_string(e.what()));
            }
        }
    }

    void PalBuildingModLoader::SetupTranslations(const RC::Unreal::FName& BuildingId, const nlohmann::json& Data)
    {
        if (Data.contains("Name"))
        {
            auto RowId = fmt::format(STR("MAPOBJECT_NAME_{}"), BuildingId.ToString());
            SetupTranslation(RowId, m_mapObjectNameTable, Data.at("Name"));
        }

        if (Data.contains("Description"))
        {
            auto RowId = fmt::format(STR("BUILDOBJECT_DESC_{}"), BuildingId.ToString());
            SetupTranslation(RowId, m_buildObjectDescTable, Data.at("Description"));
        }

        if (Data.contains("Technology"))
        {
            auto Technology = Data.at("Technology");
            if (Technology.is_object())
            {
                if (Technology.contains("Name"))
                {
                    auto RowId = fmt::format(STR("NAME_RECIPE_{}"), BuildingId.ToString());
                    SetupTranslation(RowId, m_technologyNameTable, Technology.at("Name"));
                }

                if (Technology.contains("Description"))
                {
                    auto RowId = fmt::format(STR("DESC_RECIPE_{}"), BuildingId.ToString());
                    SetupTranslation(RowId, m_technologyDescTable, Technology.at("Description"));
                }
            }
        }
    }

    void PalBuildingModLoader::SetupTranslation(const RC::StringType& RowKey, RC::Unreal::UDataTable* DataTable, const nlohmann::json& Value)
    {
        auto TranslationRowStruct = DataTable->GetRowStruct().Get();
        auto TextProperty = TranslationRowStruct->GetPropertyByName(STR("TextData"));
        if (TextProperty)
        {
            auto RowKeyName = FName(RowKey, FNAME_Add);

            auto ExistingRow = DataTable->FindRowUnchecked(RowKeyName);
            if (ExistingRow)
            {
                PropertyHelper::CopyJsonValueToContainer(ExistingRow, TextProperty, Value);
            }
            else
            {
                auto TranslationRowData = FMemory::Malloc(TranslationRowStruct->GetStructureSize());
                TranslationRowStruct->InitializeStruct(TranslationRowData);

                try
                {
                    PropertyHelper::CopyJsonValueToContainer(TranslationRowData, TextProperty, Value);
                    DataTable->AddRow(RowKeyName, *reinterpret_cast<RC::Unreal::FTableRowBase*>(TranslationRowData));
                }
                catch (const std::exception& e)
                {
                    FMemory::Free(TranslationRowData);
                    throw std::runtime_error(e.what());
                }
            }
        }
    }

    void PalBuildingModLoader::ImportJson(const RC::Unreal::FName& BuildingId, const nlohmann::json& Data, RC::Unreal::UDataTable* DataTable)
    {
        auto TableRow = DataTable->FindRowUnchecked(BuildingId);
        auto TableRowStruct = DataTable->GetRowStruct().Get();
        if (TableRow)
        {
            try
            {
                for (FProperty* Property : TFieldRange<FProperty>(TableRowStruct, EFieldIterationFlags::IncludeSuper))
                {
                    auto PropertyName = RC::to_string(Property->GetName());
                    if (Data.contains(PropertyName))
                    {
                        PropertyHelper::CopyJsonValueToContainer(TableRow, Property, Data.at(PropertyName));
                    }
                }
            }
            catch (const std::exception& e)
            {
                PS::Log<LogLevel::Error>(STR("Failed to modify Row '{}' in {}: {}\n"), BuildingId.ToString(), DataTable->GetFullName(), RC::to_generic_string(e.what()));
            }
        }
        else
        {
            auto RowData = FMemory::Malloc(TableRowStruct->GetStructureSize());
            TableRowStruct->InitializeStruct(RowData);
            try
            {
                for (FProperty* Property : TFieldRange<FProperty>(TableRowStruct, EFieldIterationFlags::IncludeSuper))
                {
                    auto PropertyName = RC::to_string(Property->GetName());
                    if (Data.contains(PropertyName))
                    {
                        PropertyHelper::CopyJsonValueToContainer(RowData, Property, Data.at(PropertyName));
                    }
                }

                DataTable->AddRow(BuildingId, *reinterpret_cast<RC::Unreal::FTableRowBase*>(RowData));
            }
            catch (const std::exception& e)
            {
                FMemory::Free(RowData);
                PS::Log<LogLevel::Error>(STR("Failed to add Row '{}' to {}: {}\n"), BuildingId.ToString(), DataTable->GetFullName(), RC::to_generic_string(e.what()));
            }
        }
    }

    RC::StringType PalBuildingModLoader::GetAssignIDSuffixByWorkType(const std::string& WorkType)
    {
        if (WorkType == "EPalWorkType::Seeding" || WorkType == "Seeding")
        {
            return STR("_5");
        }
        else if (WorkType == "EPalWorkType::Watering_Farm" || WorkType == "Watering_Farm")
        {
            return STR("_2");
        }
        else if (WorkType == "EPalWorkType::FarmHarvest" || WorkType == "FarmHarvest")
        {
            return STR("_4");
        }
        else
        {
            return STR("_0");
        }
    }

    int PalBuildingModLoader::GetNextRadialIndex()
    {
        return ++RadialIndex;
    }
}