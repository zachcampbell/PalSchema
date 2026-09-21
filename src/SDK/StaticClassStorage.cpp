#include "SDK/StaticClassStorage.h"
#include "SDK/Helper/PropertyHelper.h"
#include "Unreal/CoreUObject/UObject/Class.hpp"

using namespace RC;
using namespace RC::Unreal;

namespace Palworld {
    void StaticClassStorage::Initialize()
    {
        auto NameToFieldClassMap = Palworld::PropertyHelper::GetNameToFieldClassMap();
        for (auto& [TypeName, StaticClass] : *NameToFieldClassMap)
        {
            if (TypeName == FName(STR("ObjectProperty")))
            {
                // palhook (Linux, PalServer v1.0.5): two field classes are named ObjectProperty here, the plain
                // FObjectProperty (cast flags 0x4018001) and the FObjectPtrProperty variant (adds
                // CASTCLASS_FObjectPtrProperty 0x0020000000000000, its super is the plain one). Taking whichever
                // came last made every UAnimMontage*/UObject* reference fail IsA and go "Unhandled".
                // The name map holds one entry per name, so only the pointer variant is listed; the plain class
                // is its SuperClass.
                const bool is_ptr_variant = (StaticClass->GetCastFlags() & 0x0020000000000000ull) != 0;
                if (is_ptr_variant)
                {
                    ObjectPtrPropertyStaticClass = StaticClass;
                    auto* Super = StaticClass->SuperClass;
                    if (Super && Super->GetName() == STR("ObjectProperty"))
                    {
                        ObjectPropertyStaticClass = Super;
                        PS::Log<LogLevel::Verbose>(STR("ObjectProperty::StaticClass taken from the ObjectPtrProperty variant's super ({} -> {})\n"), static_cast<void*>(StaticClass), static_cast<void*>(Super));
                    }
                    else if (!ObjectPropertyStaticClass)
                    {
                        ObjectPropertyStaticClass = StaticClass;
                    }
                }
                else
                {
                    ObjectPropertyStaticClass = StaticClass;
                }
            }
            if (TypeName == FName(STR("ObjectPtrProperty")))
            {
                ObjectPtrPropertyStaticClass = StaticClass;
            }
            if (TypeName == FName(STR("NameProperty")))
            {
                NamePropertyStaticClass = StaticClass;
            }
            if (TypeName == FName(STR("BoolProperty")))
            {
                BoolPropertyStaticClass = StaticClass;
            }
            if (TypeName == FName(STR("ArrayProperty")))
            {
                ArrayPropertyStaticClass = StaticClass;
            }
            if (TypeName == FName(STR("MapProperty")))
            {
                MapPropertyStaticClass = StaticClass;
            }
            if (TypeName == FName(STR("StructProperty")))
            {
                StructPropertyStaticClass = StaticClass;
            }
            if (TypeName == FName(STR("ClassProperty")))
            {
                ClassPropertyStaticClass = StaticClass;
            }
            if (TypeName == FName(STR("ClassPtrProperty")))
            {
                ClassPtrPropertyStaticClass = StaticClass;
            }
            if (TypeName == FName(STR("SoftClassProperty")))
            {
                SoftClassPropertyStaticClass = StaticClass;
            }
            if (TypeName == FName(STR("SoftObjectProperty")))
            {
                SoftObjectPropertyStaticClass = StaticClass;
            }
            if (TypeName == FName(STR("EnumProperty")))
            {
                EnumPropertyStaticClass = StaticClass;
            }
            if (TypeName == FName(STR("TextProperty")))
            {
                TextPropertyStaticClass = StaticClass;
            }
            if (TypeName == FName(STR("StrProperty")))
            {
                StrPropertyStaticClass = StaticClass;
            }
            if (TypeName == FName(STR("NumericProperty")))
            {
                NumericPropertyStaticClass = StaticClass;
            }

            PS::Log<LogLevel::Verbose>(STR("Found {}::StaticClass\n"), TypeName.ToString());
        }
    }
}