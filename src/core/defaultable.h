/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *
 *  @project       Vinifera
 *
 *  @file          DEFAULTABLE.H
 *
 *  @author        ZivDero
 *
 *  @brief         Wrapper for a field with a default value provider.
 *
 *  @license       Vinifera is free software: you can redistribute it and/or
 *                 modify it under the terms of the GNU General Public License
 *                 as published by the Free Software Foundation, either version
 *                 3 of the License, or (at your option) any later version.
 *
 *                 Vinifera is distributed in the hope that it will be
 *                 useful, but WITHOUT ANY WARRANTY; without even the implied
 *                 warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *                 PURPOSE. See the GNU General Public License for more details.
 *
 *                 You should have received a copy of the GNU General Public
 *                 License along with this program.
 *                 If not, see <http://www.gnu.org/licenses/>.
 *
 ******************************************************************************/
#pragma once

#include "tibsun_defines.h"
#include "rect.h"
#include "point.h"
#include "ccini.h"
#include "hsv.h"
#include <type_traits>
#include <utility>
#include <string>

// These are required so that we can call things from findmake.h
#include "abstracttype.h"
#include "aircrafttype.h"
#include "aitrigtype.h"
#include "animtype.h"
#include "buildingtype.h"
#include "bullettype.h"
#include "campaign.h"
#include "housetype.h"
#include "infantrytype.h"
#include "overlaytype.h"
#include "particlesystype.h"
#include "particletype.h"
#include "scripttype.h"
#include "side.h"
#include "smudgetype.h"
#include "supertype.h"
#include "tagtype.h"
#include "taskforce.h"
#include "teamtype.h"
#include "terraintype.h"
#include "tiberium.h"
#include "triggertype.h"
#include "unittype.h"
#include "voxelanimtype.h"
#include "warheadtype.h"

#include "findmake.h"

class NoInitClass;

template<typename T, auto DefaultFn>
struct Defaultable
{
    static_assert(std::is_same_v<decltype(DefaultFn()), T>, "DefaultFn must return T");

public:
    constexpr Defaultable() : Value(T()), IsSet(false) {}
    constexpr Defaultable(const T& v) : Value(v), IsSet(true) {}
    constexpr Defaultable(NoInitClass const& x) {}

    constexpr operator T() const { return Value; }
    constexpr const T& Get() const { return Value; }

    constexpr void Set(const T& v)
    {
        Value = v;
        IsSet = true;
    }

    constexpr void Reset()
    {
        Value = DefaultFn();
        IsSet = false;
    }

    constexpr bool Has_Value() const { return IsSet; }

    constexpr Defaultable& operator=(const T& v)
    {
        Value = v;
        IsSet = true;
        return *this;
    }

    constexpr Defaultable& operator=(T&& v)
    {
        Value = std::move(v);
        IsSet = true;
        return *this;
    }

    constexpr bool operator==(const Defaultable& other) const { return Value == other.Value; }
    constexpr bool operator!=(const Defaultable& other) const { return Value != other.Value; }

    friend constexpr bool operator==(const Defaultable& p, const T& v) { return p.Value == v; }
    friend constexpr bool operator!=(const Defaultable& p, const T& v) { return p.Value != v; }
    friend constexpr bool operator==(const T& v, const Defaultable& p) { return v == p.Value; }
    friend constexpr bool operator!=(const T& v, const Defaultable& p) { return v != p.Value; }

    /**
     *  Templates for reading a value from an INI file.
     *  The default implementation triggers a static assert.
     */
    void Read_INI(CCINIClass const& ini, char const* section, char const* entry);

public:
    T Value;
    bool IsSet;
};

template<typename T, auto DefaultFn>
void Read_Defaultable_From_INI(Defaultable<T, DefaultFn>&, CCINIClass const&, char const*, char const*)
{
    static_assert(sizeof(T) == 0, "No INI reader specialization defined for this Defaultable<T>.");
}

template<typename T, auto DefaultFn>
void Defaultable<T, DefaultFn>::Read_INI(CCINIClass const& ini, char const* section, char const* entry)
{
    Read_Defaultable_From_INI(*this, ini, section, entry);
}

// Macro to declare INI reader specializations
#define DEFINE_INI_READER(Type, Getter) \
    template<auto DefaultFn> \
    void Read_Defaultable_From_INI(Defaultable<Type, DefaultFn>& dst, CCINIClass const& ini, char const* section, char const* entry) \
    { \
        if (ini.Is_Present(section, entry)) dst.Set(ini.Getter(section, entry, dst.Value)); \
    }

// Definitions for each supported T
DEFINE_INI_READER(int, Get_Int)
DEFINE_INI_READER(double, Get_Float)
DEFINE_INI_READER(bool, Get_Bool)
DEFINE_INI_READER(long, Get_Int)
DEFINE_INI_READER(TPoint2D<int>, Get_Point)
DEFINE_INI_READER(TPoint3D<int>, Get_Point)
DEFINE_INI_READER(TPoint3D<float>, Get_Point)
DEFINE_INI_READER(Rect, Get_Rect)
DEFINE_INI_READER(CLSID, Get_UUID)
DEFINE_INI_READER(MPHType, Get_MPHType)
DEFINE_INI_READER(PipEnum, Get_PipEnum)
DEFINE_INI_READER(PipScaleType, Get_PipScaleType)
DEFINE_INI_READER(CategoryType, Get_CategoryType)
DEFINE_INI_READER(TargetStruct, Get_xTarget)
DEFINE_INI_READER(ColorSchemeType, Get_Scheme_Index)
DEFINE_INI_READER(RGBClass, Get_RGBColor)
DEFINE_INI_READER(HSVClass, Get_HSVColor)
DEFINE_INI_READER(BSizeType, Get_BSizeType)
DEFINE_INI_READER(MZoneType, Get_MZoneType)
DEFINE_INI_READER(ActionType, Get_ActionType)
DEFINE_INI_READER(SuperWeaponType, Get_SuperWeaponType)
DEFINE_INI_READER(VoxType, Get_VoxType)
DEFINE_INI_READER(RTTIType, Get_RTTIType)
DEFINE_INI_READER(ArmorType, Get_ArmorType)
DEFINE_INI_READER(VocType, Get_VocType)
DEFINE_INI_READER(LandType, Get_LandType)
DEFINE_INI_READER(HousesType, Get_HousesType)
DEFINE_INI_READER(SideType, Get_SideType)
DEFINE_INI_READER(VQType, Get_VQType)
DEFINE_INI_READER(TheaterType, Get_TheaterType)
DEFINE_INI_READER(ThemeType, Get_ThemeType)
DEFINE_INI_READER(SourceType, Get_SourceType)
DEFINE_INI_READER(CrateType, Get_CrateType)
DEFINE_INI_READER(SpeedType, Get_SpeedType)
DEFINE_INI_READER(AbilityFlagsType, Get_Abilities)
DEFINE_INI_READER(LayerType, Get_LayerType)
DEFINE_INI_READER(Cell, Get_Cell)
DEFINE_INI_READER(TargetClass, Get_Target)
DEFINE_INI_READER(TypeList<int>, Get_IntList)
DEFINE_INI_READER(TypeList<RGBClass>, Get_RGBColors)
DEFINE_INI_READER(TypeList<TechnoTypeClass*>, Get_TechnoType_List)

template<typename T>
concept IsAbstractType = std::is_base_of_v<AbstractTypeClass, T>;

template<typename T>
concept IsAbstractTypePtr = std::is_pointer_v<T> && IsAbstractType<std::remove_pointer_t<T>>;

template<typename T, auto DefaultFn>
    requires IsAbstractTypePtr<T>
void Read_Defaultable_From_INI(Defaultable<T, DefaultFn>& dst, CCINIClass const& ini, char const* section, char const* entry)
{
    if (!ini.Is_Present(section, entry)) return;

    char buffer[1024] {};
    if (ini.Get_String(section, entry, "", buffer, sizeof(buffer)) > 0) {
        dst.Set(TGet_Class(ini, section, entry, dst.Get()));
    }
}

template<typename T, auto DefaultFn>
    requires IsAbstractTypePtr<T>
void Read_Defaultable_From_INI(Defaultable<TypeList<T>, DefaultFn>& dst, CCINIClass const& ini, char const* section, char const* entry)
{
    if (!ini.Is_Present(section, entry)) return;

    dst.Set(TGet_TypeList<T>(ini, section, entry, dst.Get()));
}
