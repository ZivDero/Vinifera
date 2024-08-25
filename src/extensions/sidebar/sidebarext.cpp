/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *
 *  @project       Vinifera
 *
 *  @file          SidebarEXT.CPP
 *
 *  @author        ZivDero
 *
 *  @brief         Extended SidebarClass class.
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
#include "sidebarext.h"
#include "sidebarext.h"
#include "tibsun_globals.h"
#include "tibsun_defines.h"
#include "ccini.h"
#include "noinit.h"
#include "swizzle.h"
#include "vinifera_saveload.h"
#include "asserthandler.h"
#include "debughandler.h"
#include "language.h"
#include "tooltip.h"
#include "mouse.h"


SidebarClass::StripClass* Column[SidebarClassExtension::SIDEBAR_TAB_COUNT];
SidebarClass::StripClass::SelectClass SelectButton[SidebarClassExtension::SIDEBAR_TAB_COUNT][SidebarClass::StripClass::MAX_BUILDABLES];

/**
 *  Class constructor.
 *  
 *  @author: ZivDero
 */
SidebarClassExtension::SidebarClassExtension(const SidebarClass *this_ptr) :
    GlobalExtensionClass(this_ptr)
{
    //if (this_ptr) EXT_DEBUG_TRACE("SidebarClassExtension::SidebarClassExtension - 0x%08X\n", (uintptr_t)(ThisPtr));

    int max_visible = Max_Visible(true);

    for (int i = 0; i < SIDEBAR_TAB_COUNT; i++)
    {
        Column[i] = new SidebarClass::StripClass(NoInitClass());
        Column[i]->X = SidebarClass::COLUMN_ONE_X;
        Column[i]->Y = SidebarClass::COLUMN_ONE_Y;
        Column[i]->Size = Rect(SidebarClass::COLUMN_ONE_X, SidebarClass::COLUMN_ONE_Y, SidebarClass::StripClass::OBJECT_WIDTH, SidebarClass::StripClass::OBJECT_HEIGHT * max_visible);
    }
}


/**
 *  Class no-init constructor.
 *  
 *  @author: ZivDero
 */
SidebarClassExtension::SidebarClassExtension(const NoInitClass &noinit) :
    GlobalExtensionClass(noinit)
{
    //EXT_DEBUG_TRACE("SidebarClassExtension::SidebarClassExtension(NoInitClass) - 0x%08X\n", (uintptr_t)(ThisPtr));
}


/**
 *  Class destructor.
 *  
 *  @author: ZivDero
 */
SidebarClassExtension::~SidebarClassExtension()
{
    //EXT_DEBUG_TRACE("SidebarClassExtension::~SidebarClassExtension - 0x%08X\n", (uintptr_t)(ThisPtr));

    for (int i = 0; i < SIDEBAR_TAB_COUNT; i++)
    {
        if (Column[i] != nullptr)
            delete Column[i];
    }
}


/**
 *  Initializes an object from the stream where it was saved previously.
 *  
 *  @author: ZivDero
 */
HRESULT SidebarClassExtension::Load(IStream *pStm)
{
    //EXT_DEBUG_TRACE("SidebarClassExtension::Load - 0x%08X\n", (uintptr_t)(This()));

    HRESULT hr = GlobalExtensionClass::Load(pStm);
    if (FAILED(hr)) {
        return E_FAIL;
    }

    new (this) SidebarClassExtension(NoInitClass());
    
    return hr;
}


/**
 *  Saves an object to the specified stream.
 *  
 *  @author: ZivDero
 */
HRESULT SidebarClassExtension::Save(IStream *pStm, BOOL fClearDirty)
{
    //EXT_DEBUG_TRACE("SidebarClassExtension::Save - 0x%08X\n", (uintptr_t)(This()));

    HRESULT hr = GlobalExtensionClass::Save(pStm, fClearDirty);
    if (FAILED(hr)) {
        return hr;
    }

    return hr;
}


/**
 *  Return the raw size of class data for save/load purposes.
 *  
 *  @author: ZivDero
 */
int SidebarClassExtension::Size_Of() const
{
    //EXT_DEBUG_TRACE("SidebarClassExtension::Size_Of - 0x%08X\n", (uintptr_t)(This()));

    return sizeof(*this);
}


/**
 *  Removes the specified target from any targeting and reference trackers.
 *  
 *  @author: ZivDero
 */
void SidebarClassExtension::Detach(TARGET target, bool all)
{
    //EXT_DEBUG_TRACE("SidebarClassExtension::Detach - 0x%08X\n", (uintptr_t)(This()));
}


/**
 *  Compute a unique crc value for this instance.
 *  
 *  @author: ZivDero
 */
void SidebarClassExtension::Compute_CRC(WWCRCEngine &crc) const
{
    //EXT_DEBUG_TRACE("SidebarClassExtension::Compute_CRC - 0x%08X\n", (uintptr_t)(This()));
}


/**
 *  Initialises any values for this instance.
 *
 *  @author: ZivDero
 */
void SidebarClassExtension::Activate()
{
    //EXT_DEBUG_TRACE("ScenarioClassExtension::Init_Clear - 0x%08X\n", (uintptr_t)(This()));

    Column[TabIndex]->Activate();
}


void SidebarClassExtension::Init_Strips()
{
    int max_visible = Max_Visible(true);

    for (int i = 0; i < SIDEBAR_TAB_COUNT; i++)
    {
        Column[i] = new SidebarClass::StripClass(NoInitClass());
        Column[i]->X = SidebarClass::COLUMN_ONE_X;
        Column[i]->Y = SidebarClass::COLUMN_ONE_Y;
        Column[i]->Size = Rect(SidebarClass::COLUMN_ONE_X, SidebarClass::COLUMN_ONE_Y, SidebarClass::StripClass::OBJECT_WIDTH, SidebarClass::StripClass::OBJECT_HEIGHT * max_visible);
    }
}


void SidebarClassExtension::Change_Tab(SidebarTabType index)
{
    Column[TabIndex]->Deactivate();
    TabIndex = index;
    Column[TabIndex]->Activate();
    Map.IsToFullRedraw = true;
}

SidebarClassExtension::SidebarTabType SidebarClassExtension::Which_Tab(RTTIType type)
{
    switch (type)
    {
    case RTTI_BUILDINGTYPE:
    case RTTI_BUILDING:
        return SIDEBAR_TAB_STRUCTURE;

    case RTTI_INFANTRYTYPE:
    case RTTI_INFANTRY:
        return SIDEBAR_TAB_INFANTRY;

    case RTTI_UNITTYPE:
    case RTTI_UNIT:
        return SIDEBAR_TAB_UNIT;

    case RTTI_AIRCRAFTTYPE:
    case RTTI_AIRCRAFT:
    case RTTI_SUPERWEAPONTYPE:
    case RTTI_SUPERWEAPON:
    case RTTI_SPECIAL:
    default:
        return SIDEBAR_TAB_SPECIAL;
    }
}
