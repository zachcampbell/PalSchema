#pragma once
// Linux shim for PalSchema's SignatureManager: no AOB scanning. Names resolve to addresses proven on
// the shadow (see /mnt/FTY/palhook/notes/palschema-signature-inventory.md) or to small adapters.
#include <string>
#include <unordered_map>
namespace Palworld {
    class SignatureManager {
    public:
        static void Initialize();
        static void* GetSignature(const std::string& ClassAndFunction);
        static inline std::unordered_map<std::string, void*> SignatureMap;
    };
}
