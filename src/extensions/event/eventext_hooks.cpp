/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *
 *  @project       Vinifera (Dawn of the Tiberium Age Build)
 *
 *  @file          EVENTEXT_HOOKS.CPP
 *
 *  @author        Rampastring
 *
 *  @brief         Contains the hooks for the extended EventClass.
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
#include "eventext_hooks.h"
#include "tibsun_globals.h"
#include "event.h"
#include "scenario.h"
#include "session.h"
#include "techno.h"

#include "debughandler.h"
#include "asserthandler.h"
#include "extension_globals.h"
#include "sidebarext.h"

#include "hooker.h"
#include "hooker_macros.h"
#include "houseext.h"
#include "unittypeext.h"


/**
  *  A fake class for implementing new member functions which allow
  *  access to the "this" pointer of the intended class.
  *
  *  @note: This must not contain a constructor or deconstructor!
  *  @note: All functions must be prefixed with "_" to prevent accidental virtualization.
  */
static class EventClassFake final : public EventClass
{
public:
    void _Remember_Last_Building(int house, EventType eventtype, RTTIType type, int id);
};


void EventClassFake::_Remember_Last_Building(int house, EventType eventtype, RTTIType type, int id)
{
    if (type == RTTI_BUILDING || type == RTTI_BUILDINGTYPE)
    {
        SidebarExtension->LastBuildingRTTI = type;
        SidebarExtension->LastBuildingHeapID = id;
    }

    new (this) EventClass(house, eventtype, type, id);
}


/**
 *  Fixes a cheat in the original game where players are able to issue
 *  commands to technos that are not owned by them.
 *
 *  Author: Rampastring
 */
DECLARE_PATCH(_EventClass_Execute_MEGAMISSION_Prevent_Controlling_Enemy_Units_Patch)
{
    enum JumpAddresses {
        Continue = 0x0049470A,
        Bail = 0x00495110
    };

    GET_REGISTER_STATIC(EventClass*, this_ptr, esi);
    GET_REGISTER_STATIC(TechnoClass*, techno, edi);

    if (Session.Type != GAME_NORMAL) {
        // In multiplayer, each human player can only control one house.
        if (this_ptr->ID != techno->House->ID) {
            // ID of owner of techno does not match the ID of whoever generated the event.
            // Exit the function.
            JMP(Bail);
        }
    }
    else {
        // In campaign, the player can control multiple houses.
        // We might as well also fix this exploit for campaign by checking for player control here.
        if (!techno->House->IsPlayerControl) {
            JMP(Bail);
        }
    }

    // Stolen bytes / code.
    // Jump out if the techno is not active.
    if (!techno->IsActive) {
        JMP(Bail);
    }

    // Continue event execution.
    JMP(Continue);
}


/**
 *  Fixes a cheat in the original game where players are able to issue
 *  an IDLE command to technos that are not owned by them.
 *
 *  Author: Rampastring
 */
DECLARE_PATCH(_EventClass_Execute_IDLE_Prevent_Controlling_Enemy_Units_Patch)
{
    enum JumpAddresses {
        Continue = 0x004949BB,
        Bail = 0x00495110
    };

    GET_REGISTER_STATIC(EventClass*, this_ptr, esi);
    GET_REGISTER_STATIC(TechnoClass*, techno, eax);

    // Stolen bytes / code.
    // Jump out if the techno is null.
    if (techno == nullptr) {
        JMP(Bail);
    }

    if (Session.Type != GAME_NORMAL) {
        // In multiplayer, each human player can only control one house.
        if (this_ptr->ID != techno->House->ID) {
            // ID of owner of techno does not match the ID of whoever generated the event.
            // Exit the function.
            JMP(Bail);
        }
    }
    else {
        // In campaign, the player can control multiple houses.
        // We might as well also fix this exploit for campaign by checking for player control here.
        if (!techno->House->IsPlayerControl) {
            JMP(Bail);
        }
    }

    // Continue event execution.
    // Set esi to point to the techno and edi to zero, the original 
    // game code expects these values.
    _asm { mov  esi, dword ptr ds:techno }
    _asm { xor  edi, edi }
    JMP(Continue);
}


/**
 *  Fixes a cheat in the original game where players are able to issue
 *  a PRIMARY command to buildings that are not owned by them.
 *
 *  Author: Rampastring
 */
DECLARE_PATCH(_EventClass_Execute_PRIMARY_Prevent_Setting_For_Enemy_Patch)
{
    enum JumpAddresses {
        Continue = 0x004946D8,
        Bail = 0x00495110
    };

    GET_REGISTER_STATIC(EventClass*, this_ptr, esi);
    GET_REGISTER_STATIC(TechnoClass*, techno, eax);

    // Stolen bytes / code.
    if (!techno->IsActive) {
        JMP(Bail);
    }

    // Make sure that the owner of the building is the same player who sent the PRIMARY event.
    if (this_ptr->ID != techno->House->ID) {
        JMP(Bail);
    }

    _asm { mov  eax, dword ptr ds:techno }
    JMP_REG(edx, Continue);
}


DECLARE_PATCH(_EventClass_Execute_PRODUCE)
{
    GET_REGISTER_STATIC(EventClass*, this_ptr, esi);
    GET_REGISTER_STATIC(HouseClass*, house, ebx);

    HouseClassExtension::Begin_Production_IsNaval.Set(UnitTypeClassExtension::Is_Naval(this_ptr->Data.Specific.Type, this_ptr->Data.Specific.ID));
    house->Begin_Production(this_ptr->Data.Specific.Type, this_ptr->Data.Specific.ID, false);
    JMP(0x00494682);
}


DECLARE_PATCH(_EventClass_Execute_SUSPEND)
{
    GET_REGISTER_STATIC(EventClass*, this_ptr, esi);
    GET_REGISTER_STATIC(HouseClass*, house, ebx);

    HouseClassExtension::Suspend_Production_IsNaval.Set(UnitTypeClassExtension::Is_Naval(this_ptr->Data.Specific.Type, this_ptr->Data.Specific.ID));
    house->Suspend_Production(this_ptr->Data.Specific.Type);
    JMP(0x00494698);
}


DECLARE_PATCH(_EventClass_Execute_ABANDON)
{
    GET_REGISTER_STATIC(EventClass*, this_ptr, esi);
    GET_REGISTER_STATIC(HouseClass*, house, ebx);

    HouseClassExtension::Abandon_Production_IsNaval.Set(UnitTypeClassExtension::Is_Naval(this_ptr->Data.Specific.Type, this_ptr->Data.Specific.ID));
    house->Abandon_Production(this_ptr->Data.Specific.Type, this_ptr->Data.Specific.ID);
    JMP(0x004946B2);
}

DECLARE_PATCH(_EventClass_Execute_PLACE)
{
    GET_REGISTER_STATIC(EventClass*, this_ptr, esi);
    GET_REGISTER_STATIC(HouseClass*, house, ebx);

    HouseClassExtension::Place_Object_IsNaval.Set(UnitTypeClassExtension::Is_Naval(this_ptr->Data.Specific.Type, this_ptr->Data.Specific.ID));
    house->Place_Object(this_ptr->Data.Place.Type, Cell(this_ptr->Data.Place.Cell.X & 0xFFFF, this_ptr->Data.Place.Cell.X >> 16));
    JMP(0x00494666);
}


/**
 *  Main function for patching the hooks.
 */
void EventClassExtension_Hooks()
{
    Patch_Jump(0x004946FF, &_EventClass_Execute_MEGAMISSION_Prevent_Controlling_Enemy_Units_Patch);
    Patch_Jump(0x004949AF, &_EventClass_Execute_IDLE_Prevent_Controlling_Enemy_Units_Patch);
    Patch_Jump(0x004946CD, &_EventClass_Execute_PRIMARY_Prevent_Setting_For_Enemy_Patch);
    Patch_Jump(0x00494671, &_EventClass_Execute_PRODUCE);
    Patch_Jump(0x0049468D, &_EventClass_Execute_SUSPEND);
    Patch_Jump(0x004946A3, &_EventClass_Execute_ABANDON);
    Patch_Jump(0x00494644, &_EventClass_Execute_PLACE);

    Patch_Call(0x005F5DE9, &EventClassFake::_Remember_Last_Building);
}
