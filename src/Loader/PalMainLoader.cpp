#include <fstream>
#include <fmt/format.h>
#include <fmt/xchar.h>
#include <filesystem>
#include "Unreal/CoreUObject/UObject/Class.hpp"
#include "Unreal/UFunction.hpp"
#include "Unreal/Hooks.hpp"
#include "Utility/Config.h"
#include "Utility/Logging.h"
#include "SDK/Classes/Async.h"
#include "SDK/Classes/Custom/UDataTableStore.h"
#include "SDK/Classes/Custom/UObjectGlobals.h"
#include "SDK/Classes/UCompositeDataTable.h"
#include "SDK/Classes/UWorldPartitionRuntimeLevelStreamingCell.h"
#include "SDK/Classes/PalUtility.h"
#include "SDK/PalSignatures.h"
#include "SDK/StaticClassStorage.h"
#include "SDK/UnrealOffsets.h"
#include "UE4SSProgram.hpp"
#include "Loader/PalMonsterModLoader.h"
#include "Loader/PalHumanModLoader.h"
#include "Loader/PalLanguageModLoader.h"
#include "Loader/PalItemModLoader.h"
#include "Loader/PalSkinModLoader.h"
#include "Loader/PalAppearanceModLoader.h"
#include "Loader/PalBuildingModLoader.h"
#include "Loader/PalRawTableLoader.h"
#include "Loader/PalBlueprintModLoader.h"
#include "Loader/PalEnumLoader.h"
#include "Loader/PalHelpGuideModLoader.h"
#include "Loader/PalSpawnLoader.h"
#include "Loader/PalMainLoader.h"
#include "Misc/FileWatchWrapper.h"
#ifdef __linux__
#include <Unreal/UObjectArray.hpp>
namespace
{
    // palhook: the engine tells its delete listeners about every destroyed UObject; a registered table that goes
    // away is dropped from the registry so no loader ever dereferences a dead pointer. Same Itanium vtable
    // order as the engine's FUObjectDeleteListener (dtor, NotifyUObjectDeleted, OnUObjectArrayShutdown).
    struct RegistryDeleteListener final : RC::Unreal::FUObjectDeleteListener
    {
        UECustom::UDataTableRegistry* Registry = nullptr;
        int Dropped = 0;
        bool Installed = false;
        // Called by the engine for every destroyed object, including during shutdown when UE4SS's output may
        // already be gone: never log from here, only drop the pointer and count.
        void NotifyUObjectDeleted(const RC::Unreal::UObjectBase* Object, RC::Unreal::int32) override
        {
            if (Registry && Registry->Contains(Object) && Registry->Remove(Object)) ++Dropped;
        }
        // The engine calls this from FUObjectArray::ShutdownUObjectArray (reverse iteration) and then asserts that
        // no listener is left registered, so the listener removes itself here.
        void OnUObjectArrayShutdown() override { Uninstall(); }
        void Uninstall()
        {
            if (!Installed) return;
            RC::Unreal::UObjectArray::RemoveUObjectDeleteListener(this);
            Installed = false; Registry = nullptr;
        }
    };
    RegistryDeleteListener g_registry_delete_listener;
}
#endif


using namespace RC;
using namespace RC::Unreal;

namespace fs = std::filesystem;

namespace Palworld {
    PalMainLoader::PalMainLoader() {
        CreateLoaders();
    }

    PalMainLoader::~PalMainLoader()
    {
#ifdef __linux__
        if (g_registry_delete_listener.Installed)
        {
            PS::Log<LogLevel::Normal>(STR("Registry delete listener removed ({} tables dropped while installed).\n"), g_registry_delete_listener.Dropped);
            g_registry_delete_listener.Uninstall();
        }
#endif
        auto expected1 = DatatableSerialize_Hook.disable();
        DatatableSerialize_Hook = {};

        auto expected2 = GameInstanceInit_Hook.disable();
        GameInstanceInit_Hook = {};

        auto expected4 = GetPakFolders_Hook.disable();
        GetPakFolders_Hook = {};

        DatatableSerializeCallbacks.clear();
        GameInstanceInitCallbacks.clear();
        GetPakFoldersCallback.clear();
    }

    void PalMainLoader::PreInitialize()
    {
        HookDatatableSerialize();
        SetupAlternativePakPathReader();
    }

    void PalMainLoader::Initialize()
	{
        SetupAutoReload();
	}

    void PalMainLoader::AutoReload(const std::filesystem::path& filePath)
    {
        // Skip to the PalSchema folder and start our iterator from there
        auto it = std::find_if(filePath.begin(), filePath.end(),
            [](const auto& p) { return p == "PalSchema"; });

        if (it == filePath.end() || std::distance(it, filePath.end()) < 4)
        {
            return;
        }

        // Skip PalSchema and mods folder
        std::advance(it, 2);
        auto modName = RC::to_generic_string(it->string());

        // Move to folder type, e.g. buildings
        std::advance(it, 1);
        auto folderType = it->string();

        std::ifstream f(filePath);
        if (f.peek() == std::ifstream::traits_type::eof()) {
            return;
        }
        f.close();

        UECustom::AsyncTask(UECustom::ENamedThreads::GameThread, [this, filePath, folderType, modName]() {
            try
            {
                for (auto& loader : m_loaders)
                {
                    if (loader->GetModFolderType() == folderType)
                    {
                        loader->AutoReload(modName, filePath);
                        PS::Log<LogLevel::Normal>(STR("Auto-reloaded mod {}\n"), modName);
                        break;
                    }
                }
            }
            catch (const std::exception& e)
            {
                PS::Log<LogLevel::Error>(STR("Failed to auto-reload mod {} - {}\n"), modName, RC::to_generic_string(e.what()));
            }
        });
    }

    void PalMainLoader::IterateModsFolder(const std::function<void(const std::filesystem::path&, const RC::StringType&)>& callback)
    {
        static auto modsPath = fs::path(UE4SSProgram::get_program().get_working_directory()) / "Mods" / "PalSchema" / "mods";
        if (fs::exists(modsPath))
        {
            for (const auto& entry : fs::directory_iterator(modsPath)) {
                if (entry.is_directory())
                {
                    auto& path = entry.path();
                    auto folderName = RC::to_generic_string(path.stem().string());
                    callback(entry.path(), folderName);
                }
            }
        }
    }

    void PalMainLoader::SetupPostEngineInitLoaders()
    {
        InitializeMods(EEngineLifecyclePhase::PostEngineInit);
        LoadMods(EEngineLifecyclePhase::PostEngineInit);
    }

    void PalMainLoader::SetupGameInstanceInitLoaders()
    {
        InitializeMods(EEngineLifecyclePhase::GameInstanceInit);
        LoadMods(EEngineLifecyclePhase::GameInstanceInit);
    }

    void PalMainLoader::HookDatatableSerialize()
    {
        auto DatatableSerializeFuncPtr = Palworld::SignatureManager::GetSignature("UDataTable::Serialize");
        if (!DatatableSerializeFuncPtr)
        {
            PS::Log<LogLevel::Error>(STR("Unable to initialize PalSchema core, signature for UDataTable::Serialize is outdated.\n"));
            return;
        }

        DatatableSerialize_Hook = safetyhook::create_inline(reinterpret_cast<void*>(DatatableSerializeFuncPtr),
            OnDataTableSerialized);

        DatatableSerializeCallbacks.push_back([&](RC::Unreal::UDataTable* datatable) {
            InitCore();
            m_datatableRegistry.Add(datatable);
        });

        PS::Log<LogLevel::Verbose>(STR("Core pre-initialized.\n"));
    }

    void PalMainLoader::HookGameInstanceInit()
    {
        auto PalGameInstanceClass = UECustom::UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/Pal.PalGameInstance"));
        if (!PalGameInstanceClass)
        {
            PS::Log<LogLevel::Error>(STR("Failed to find PalGameInstance. Cannot hook OnGameInstanceInit.\n"));
            return;
        }

        PS::Log<LogLevel::Verbose>(STR("Fetching default object for UPalGameInstance...\n"));
        uintptr_t** PGIVTablePtr = *(uintptr_t***)PalGameInstanceClass->GetClassDefaultObject();
#ifdef __linux__
        // palhook: Itanium vtables carry two destructor slots, so the Windows index 90 is 91 here (baseline +8 rule).
        void* GameInstanceInitPtr = (void*)PGIVTablePtr[91];
#else
        void* GameInstanceInitPtr = (void*)PGIVTablePtr[90];
#endif
        PS::Log<LogLevel::Verbose>(STR("Found UPalGameInstance::Init: {}\n"), GameInstanceInitPtr);

        GameInstanceInitCallbacks.push_back([&](UObject* Instance) {
            SetupGameInstanceInitLoaders();
        });

        GameInstanceInit_Hook = safetyhook::create_inline(GameInstanceInitPtr,
            reinterpret_cast<void*>(OnGameInstanceInit));
    }

    void PalMainLoader::CreateLoaders()
    {
        auto resourceLoader = std::make_unique<PalResourceLoader>();
        RegisterLoader(std::move(resourceLoader));

        auto enumLoader = std::make_unique<PalEnumLoader>();
        RegisterLoader(std::move(enumLoader));

        auto monsterModLoader = std::make_unique<PalMonsterModLoader>();
        RegisterLoader(std::move(monsterModLoader));

        auto humanModLoader = std::make_unique<PalHumanModLoader>();
        RegisterLoader(std::move(humanModLoader));

        auto itemModLoader = std::make_unique<PalItemModLoader>();
        RegisterLoader(std::move(itemModLoader));

        auto skinModLoader = std::make_unique<PalSkinModLoader>();
        RegisterLoader(std::move(skinModLoader));

        auto appearanceModLoader = std::make_unique<PalAppearanceModLoader>();
        RegisterLoader(std::move(appearanceModLoader));

        auto buildingModLoader = std::make_unique<PalBuildingModLoader>();
        RegisterLoader(std::move(buildingModLoader));

        auto rawTableModLoader = std::make_unique<PalRawTableLoader>();
        RegisterLoader(std::move(rawTableModLoader));

        auto blueprintModLoader = std::make_unique<PalBlueprintModLoader>();
        RegisterLoader(std::move(blueprintModLoader));

        auto helpGuideLoader = std::make_unique<PalHelpGuideModLoader>();
        RegisterLoader(std::move(helpGuideLoader));

        auto spawnLoader = std::make_unique<PalSpawnLoader>();
        RegisterLoader(std::move(spawnLoader));

        auto languageModLoader = std::make_unique<PalLanguageModLoader>();
        RegisterLoader(std::move(languageModLoader));
    }

    void PalMainLoader::SetupAutoReload()
    {
        auto config = PS::PSConfig::Get();
        if (!config->IsAutoReloadEnabled()) return;

        PS::Log<LogLevel::Normal>(STR("Auto-reload is enabled.\n"));

        auto modsPath = GetModsPath();

        m_fileWatcher = std::make_unique<PS::FileWatchWrapper>(modsPath, [this](efsw::WatchID watchId, const std::string& dir,
            const std::string& filename, efsw::Action action,
            std::string oldFilename) {
                if (action == efsw::Actions::Add || action == efsw::Actions::Modified)
                {
                    auto path = fs::path(dir) / filename;
                    AutoReload(path);
                }
            }
        );
        m_fileWatcher->Watch();
    }

    void PalMainLoader::SetupAlternativePakPathReader()
    {
        auto GetPakFolders_Address = Palworld::SignatureManager::GetSignature("FPakPlatformFile::GetPakFolders");
        if (GetPakFolders_Address)
        {
            GetPakFolders_Hook = safetyhook::create_inline(reinterpret_cast<void*>(GetPakFolders_Address),
                GetPakFolders);
        }
        else
        {
            PS::Log<LogLevel::Error>(STR("Unable to setup additional .pak read directory, signature for FPakPlatformFile::GetPakFolders is outdated.\n"));
        }
    }

    void PalMainLoader::InitCore()
    {
        if (m_hasInit) return;
        m_hasInit = true;

        PS::Log<LogLevel::Verbose>(STR("Initializing Static Class Storage...\n"));
        Palworld::StaticClassStorage::Initialize();

#ifdef __linux__
        // palhook: on a dedicated server the core tables load at boot, before UE4SS starts mods, so the
        // Serialize hook never sees them and the registry would not know DT_PalMonsterParameter, the
        // server's own text tables, etc. Seed it with every UDataTable already alive.
        {
            RC::Unreal::TArray<RC::Unreal::UObject*> Tables;
            if (auto* TableClass = UECustom::UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/Engine.DataTable")))
                UECustom::UObjectGlobals::GetObjectsOfClass(TableClass, Tables);
            int Seeded = 0;
            for (auto* Object : Tables) { if (Object) { m_datatableRegistry.Add(static_cast<RC::Unreal::UDataTable*>(Object)); m_seededTables.push_back(static_cast<RC::Unreal::UDataTable*>(Object)); ++Seeded; } }
            PS::Log<LogLevel::Normal>(STR("Registry seeded with {} data tables already loaded before PalSchema started.\n"), Seeded);
            if (!g_registry_delete_listener.Registry)
            {
                g_registry_delete_listener.Registry = &m_datatableRegistry;
                RC::Unreal::UObjectArray::AddUObjectDeleteListener(&g_registry_delete_listener);
                g_registry_delete_listener.Installed = true;
                PS::Log<LogLevel::Normal>(STR("Registry delete listener installed.\n"));
            }
        }
#endif

        SetupPostEngineInitLoaders();

        HookGameInstanceInit();
#ifdef __linux__
        // palhook: on a dedicated server UPalGameInstance::Init already ran at boot, before this hook existed,
        // so the GameInstanceInit-phase loaders would never run. If an instance exists, run them now.
        {
            // The instance is a Blueprint subclass (BP_PalGameInstance_C on the shadow) that UE4SS's object
            // iteration does not visit, so read it from the engine object's reflected GameInstance property.
            RC::Unreal::UObject* Instance = nullptr;
            if (auto* EngineClass = UECustom::UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/Engine.GameEngine")))
            {
                RC::Unreal::TArray<RC::Unreal::UObject*> Engines;
                UECustom::UObjectGlobals::GetObjectsOfClass(EngineClass, Engines);
                if (auto* GameInstanceProperty = EngineClass->FindProperty(FName(STR("GameInstance"), FNAME_Find)))
                    for (auto* Engine : Engines)
                        if (Engine && !Instance) Instance = *reinterpret_cast<RC::Unreal::UObject**>(reinterpret_cast<uint8_t*>(Engine) + GameInstanceProperty->GetOffset_Internal());
            }
            PS::Log<LogLevel::Normal>(STR("Game instance from the engine object: {} ({})\n"), static_cast<void*>(Instance), Instance ? Instance->GetClassPrivate()->GetName() : STR("none"));
            if (Instance)
            {
                // InitCore runs inside the Serialize hook, on the async loading thread; loaders of this phase do
                // blocking asset loads (run 59 deadlocked there), so they run on the game thread instead.
                PS::Log<LogLevel::Normal>(STR("Game instance already initialized before PalSchema started; GameInstanceInit loaders queued for the game thread.\n"));
                UECustom::AsyncTask(UECustom::ENamedThreads::GameThread, [this]() {
                    // palhook: tables that were already loaded at boot never pass through the Serialize hook, so the
                    // loaders that apply on serialize (raw tables, pending translations) never saw them: a raw edit to
                    // DT_PalBPClass silently did nothing (Paldemonium, run 114). Replay the serialize notification for
                    // every seeded table, on the game thread, before the GameInstanceInit loaders that depend on it.
                    int Replayed = 0;
                    for (auto* Table : m_seededTables)
                    {
                        if (!Table || !m_datatableRegistry.Contains(Table)) continue;
                        for (auto& Loader : m_loaders) Loader->OnDatatableSerialized(Table);
                        ++Replayed;
                    }
                    PS::Log<LogLevel::Normal>(STR("Replayed the serialize notification for {} boot-loaded data tables.\n"), Replayed);
                    PS::Log<LogLevel::Normal>(STR("Running GameInstanceInit loaders on the game thread.\n"));
                    SetupGameInstanceInitLoaders();
                });
            }
        }
#endif

        PS::Log<LogLevel::Verbose>(STR("Initialized Core\n"));
    }

    void PalMainLoader::RegisterLoader(std::unique_ptr<PalModLoaderBase> newLoader)
    {
        newLoader->AssignDatatableRegistry(m_datatableRegistry);
        newLoader->Setup();

        m_loaders.push_back(std::move(newLoader));
    }

    void PalMainLoader::InitializeMods(EEngineLifecyclePhase engineLifecyclePhase)
    {
        for (auto& loader : m_loaders)
        {
            loader->Initialize(engineLifecyclePhase);
        }
    }

    void PalMainLoader::LoadMods(EEngineLifecyclePhase engineLifecyclePhase)
    {
        IterateModsFolder([&](const fs::path& modPath, const RC::StringType& modName)
        {
            try
            {
                PS::Log<RC::LogLevel::Normal>(STR("Loading mod: {}\n"), modName);

                for (auto& loader : m_loaders)
                {
                    loader->Load(modPath, modName, engineLifecyclePhase);
                }
            }
            catch (const std::exception& e)
            {
                PS::Log<LogLevel::Error>(STR("Failed to load mod {} - {}\n"), modName, RC::to_generic_string(e.what()));
            }
        });
    }

    std::filesystem::path PalMainLoader::GetModsPath()
    {
        static auto modsPath = fs::path(UE4SSProgram::get_program().get_working_directory()) / "Mods" / "PalSchema" / "mods";
        return modsPath;
    }

    // This entire function block will get called twice, it's fine.
    void PalMainLoader::GetPakFolders(const TCHAR* CmdLine, TArray<FString>* OutPakFolders)
    {
        PS::Log<LogLevel::Verbose>(STR("Calling original FPakPlatformFile::GetPakFolders...\n"));
        GetPakFolders_Hook.call(CmdLine, OutPakFolders);

        try
        {
            // Calling this here, because we want GMalloc to be available ASAP inside this hook so we can make our changes to the TArray.
            // Once UE4SS starts running things on Game Thread, this could be moved to PreInitialize.
            // Just for clarity, there is a check inside InitializeGMalloc to prevent it from running twice since GetPakFolders runs twice.
            UnrealOffsets::InitializeGMalloc();
        }
        catch (const std::exception& e)
        {
            PS::Log<LogLevel::Error>(STR("Failed to initialize GMalloc early: {}\n"), RC::to_generic_string(e.what()));
            PS::Log<LogLevel::Error>(STR("PalSchema won't be able to load paks from the PalSchema/mods folder.\n"));
            return;
        }
        
        PS::Log<LogLevel::Verbose>(STR("Preparing to add extra .pak read directory...\n"));
        auto ModsFolderPath = GetModsPath();
        auto AbsolutePath = RC::to_generic_string(ModsFolderPath.string());
        auto AbsolutePathWithSuffix = fmt::format(STR("{}/"), RC::to_generic_string(AbsolutePath));

        PS::Log<LogLevel::Verbose>(STR("Setting extra .pak read directory to {}\n"), AbsolutePathWithSuffix);

        // If GMalloc isn't properly initialized, accessing the TArray will crash.
        OutPakFolders->Add(FString(AbsolutePathWithSuffix.c_str()));

        PS::Log<LogLevel::Verbose>(STR("Added extra .pak read directory at {}\n"), AbsolutePathWithSuffix);
    }

    void PalMainLoader::OnDataTableSerialized(RC::Unreal::UDataTable* This, RC::Unreal::FArchive* Archive)
    {
        DatatableSerialize_Hook.call(This, Archive);

        for (auto& Callback : DatatableSerializeCallbacks)
        {
            Callback(This);
        }
    }

    void PalMainLoader::OnGameInstanceInit(RC::Unreal::UObject* This)
    {
        GameInstanceInit_Hook.call(This);

        for (auto& Callback : GameInstanceInitCallbacks)
        {
            Callback(This);
        }
    }
}
