/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *
 *  @project       Vinifera
 *
 *  @file          EVENTEXT_HOOKS.CPP
 *
 *  @author        ZivDero, Rampastring
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
#include "event.h"
#include "eventext.h"

#include "tibsun_globals.h"
#include "foot.h"
#include "scenario.h"
#include "session.h"
#include "techno.h"

#include "fatal.h"
#include "debughandler.h"
#include "asserthandler.h"

#include "hooker.h"
#include "hooker_macros.h"
#include "house.h"


/**
 *  This patch intercepts EventClass::Execute and executes the event if it's one of ours.
 *
 *  @author: ZivDero
 */
DECLARE_PATCH(_EventClass_Execute_New_Events)
{
    GET_REGISTER_STATIC(EventClassExt*, event, esi);

    _asm pushad

    if (event->Is_Vinifera_Event()) {
        event->Execute();
        _asm popad
        JMP(0x00495110); // return
    }

    static EventType etype;
    static int eID;

    etype = event->Type;
    eID = event->ID;

    // continue execution
    _asm popad
    _asm mov al, etype
    _asm mov edi, eID
    JMP_REG(ebx, 0x00494299);
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
    static FootClass* cargoobject;
    static bool hasowncargo;

    // Stolen bytes / code.
    // Jump out if the techno is not active.
    if (!techno->IsActive) {
        JMP(Bail);
    }

    hasowncargo = false;

    if (Session.Type != GAME_NORMAL) {
        // In multiplayer, each human player can only control one house.
        if (this_ptr->ID != techno->House->HeapID) {

            // House IDs between event and unit do not match.
            // This might be a crafted event.
            // But before assuming so, check for the object having the event sender's units as cargo.
            // This is necessary so that a player is able to unload units placed inside another house's transport.
            cargoobject = techno->Cargo.Attached_Object();
            while (cargoobject != nullptr) {
                if (cargoobject->House->HeapID == this_ptr->ID) {
                    hasowncargo = true;
                    break;
                }
                cargoobject = reinterpret_cast<FootClass*>(cargoobject->Next);
            }

            if (!hasowncargo) {
                JMP(Bail);
            }
        }
    }
    else {
        // In campaign, the player can control multiple houses.
        // We might as well also fix this exploit for campaign by checking for player control here.
        if (!techno->House->IsPlayerControl) {

            // Also check for cargo in singleplayer. But use IsPlayerControl instead of direct ID comparison
            // due to the human player being able to control multiple houses.
            cargoobject = techno->Cargo.Attached_Object();
            while (cargoobject != nullptr) {
                if (cargoobject->House->IsPlayerControl) {
                    hasowncargo = true;
                    break;
                }
                cargoobject = reinterpret_cast<FootClass*>(cargoobject->Next);
            }

            if (!hasowncargo) {
                JMP(Bail);
            }
        }
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
        if (this_ptr->ID != techno->House->HeapID) {
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
DECLARE_PATCH(_EventClass_Executre_PRIMARY_Prevent_Setting_For_Enemy_Patch)
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
    if (this_ptr->ID != techno->House->HeapID) {
        JMP(Bail);
    }

    _asm { mov  eax, dword ptr ds:techno }
    JMP_REG(edx, Continue);
}
/**
 *  Main function for patching the hooks.
 */
void EventClassExtension_Hooks()
{
    Patch_Jump(0x00494294, &_EventClass_Execute_New_Events);
    Patch_Jump(0x004946FF, &_EventClass_Execute_MEGAMISSION_Prevent_Controlling_Enemy_Units_Patch);
    Patch_Jump(0x004949AF, &_EventClass_Execute_IDLE_Prevent_Controlling_Enemy_Units_Patch);
    Patch_Jump(0x004946CD, &_EventClass_Executre_PRIMARY_Prevent_Setting_For_Enemy_Patch);
}
