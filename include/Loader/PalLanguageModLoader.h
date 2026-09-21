#pragma once
#include <unordered_map>
#include "nlohmann/json.hpp"

#include "nlohmann/json.hpp"
#include "Loader/PalModLoaderBase.h"

namespace Palworld {
	class PalLanguageModLoader : public PalModLoaderBase {
	public:
		PalLanguageModLoader();

		~PalLanguageModLoader();
    protected:
        virtual void OnLoad(const std::filesystem::path& loaderPath, const RC::StringType& modName, const EEngineLifecyclePhase& engineLifecyclePhase) override final;
        virtual void OnAutoReload(const RC::StringType& modName, const std::filesystem::path& modFilePath) override final;
#ifdef __linux__
        // palhook: on a dedicated server most text tables serialize after the loader ran, so translations for
        // tables not yet registered are kept and applied when the table registers.
        virtual void OnDatatableSerialized(RC::Unreal::UDataTable* datatable) override;
        void ApplyTranslationsToTable(RC::Unreal::UDataTable* Table, const nlohmann::json& Rows);
        std::unordered_map<std::string, nlohmann::json> m_pendingTranslations;
#endif

        virtual bool CanInitialize(const EEngineLifecyclePhase& engineLifecyclePhase) override final;
        virtual bool OnInitialize() override final;
	private:
		std::string m_currentLanguage{};

        void LoadTranslations(const nlohmann::json& data);

        const std::string& GetCurrentLanguage();
	};
}