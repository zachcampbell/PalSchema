#pragma once
#include <unordered_set>

#include <functional>
#include <unordered_map>
#include <string>
#include <mutex>
#include "Unreal/Core/HAL/Platform.hpp"

namespace RC::Unreal {
    class UDataTable;
}

namespace UECustom {
    using DatatableSerializeCallback = std::function<void(RC::Unreal::UDataTable*)>;
    using DatatableSerializeCallbackId = RC::Unreal::uint64;

    class UDataTableRegistry {
    public:
        RC::Unreal::UDataTable* GetDatatableByName(const std::string& name);

        DatatableSerializeCallbackId RegisterDatatableSerializeCallback(const DatatableSerializeCallback& callback);
        void UnregisterDatatableSerializeCallback(const DatatableSerializeCallbackId& callbackId);

        void Add(const std::string& name, RC::Unreal::UDataTable* datatable);

        void Add(RC::Unreal::UDataTable* datatable);
#ifdef __linux__
        // palhook: drop a table the engine destroyed (seeded or hooked alike); driven by an FUObjectDeleteListener.
        bool Remove(const void* datatable);
        bool Contains(const void* datatable);
#endif
    private:
#ifdef __linux__
        std::unordered_set<const void*> m_registered;
#endif
        std::mutex m_mutex;
        std::unordered_map<std::string, RC::Unreal::UDataTable*> m_datatableMap;
        std::unordered_map<std::string, RC::Unreal::UDataTable*> m_parentTableNameToCompositeDatatableMap;
        std::unordered_map<DatatableSerializeCallbackId, DatatableSerializeCallback> m_callbackMap;

        static DatatableSerializeCallbackId GenerateDatatableSerializeCallbackId();
    };
}