/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *
 *  @project       Vinifera (Dawn of the Tiberium Age Build)
 *
 *  @file          SCROLLEXT_HOOKS.CPP
 *
 *  @author        Rampastring
 *
 *  @brief         Contains the hooks for the extended ScrollClass.
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
#include "building.h"
#include "house.h"
#include "housetype.h"
#include "mouse.h"
#include "particlesysext_hooks.h"
#include "particlesys.h"
#include "rules.h"
#include "scenario.h"
#include "techno.h"
#include "unit.h"
#include "tibsun_globals.h"
#include "tibsun_defines.h"
#include "fatal.h"
#include "debughandler.h"
#include "asserthandler.h"

#include "hooker.h"
#include "hooker_macros.h"
#include "imgui.h"
#include <windowsx.h>


bool Passes_Cloak_Check(TechnoClass* techno)
{
	if (PlayerPtr->Is_Ally(techno))
	{
		return true;
	}

	Coord coord = techno->Center_Coord();
	const CellClass* cellptr = &Map[coord];
	if (cellptr->Sensed_By(PlayerPtr->HeapID))
	{
		return true;
	}

	return false;
}


/**
 *  Fixes a bug where mouse input is not handled on cloaked objects of allied players.
 *  Sadly, the bug appears to be caused by a function that is inlined or copy-pasted
 *  in multiple places, so we need multiple patches.
 *
 *  Author: Rampastring
 */
DECLARE_PATCH(_ScrollClass_Input_Allied_Cloaked_Object_Patch1)
{
	GET_REGISTER_STATIC(TechnoClass*, techno, esi);

	if (Passes_Cloak_Check(techno))
	{
		// Target object is cloaked but visible to us (allied or sensed),
		// handle mouse input on it normally.
		JMP(0x005E886B);
	}

	// Target object is cloaked and not visible to us.
	JMP(0x005E887F);
}


/**
 *  Fixes a bug where mouse input is not handled on cloaked buildings of allied players.
 *
 *  Author: Rampastring
 */
DECLARE_PATCH(_ScrollClass_Input_Allied_Cloaked_Object_Patch2)
{
	GET_REGISTER_STATIC(BuildingClass*, building, edi);

	if (Passes_Cloak_Check(building))
	{
		// Target object is visible to us (allied or sensed), handle mouse input on it normally.
		JMP(0x005E88D6);
	}

	// Target object is cloaked and not visible to us.
	JMP(0x005E88E6);
}


/**
 *  Fixes a bug where mouse input is not handled on cloaked objects of allied players.
 *  While this is in TacticalClass, the bug is identical to the bugs in ScrollClass
 *  and has an identical fix, which is why we include this hack here.
 *
 *  Author: Rampastring
 */
DECLARE_PATCH(_Tactical_Get_Object_At_Cell_Allied_Cloaked_Object_Patch)
{
	GET_REGISTER_STATIC(TechnoClass*, techno, eax);

	if (Passes_Cloak_Check(techno))
	{
		// Target object is visible to us (allied or sensed), handle mouse input on it normally.
		JMP(0x0061680F);
	}

	// Target object is cloaked and not visible to us.
	JMP(0x00616811);
}


bool Handle_ImGui_Message(UINT message, LPARAM lparam, WPARAM wparam)
{
    // Early-out if ImGui wants this input
    ImGuiIO& io = ImGui::GetIO();
    bool imgui_consumed = false;

    switch (message) {
    case WM_LBUTTONDOWN:
        io.AddMouseButtonEvent(0, true);
        imgui_consumed = io.WantCaptureMouse;
        break;
    case WM_LBUTTONUP:
        io.AddMouseButtonEvent(0, false);
        imgui_consumed = io.WantCaptureMouse;
        break;
    case WM_RBUTTONDOWN:
        io.AddMouseButtonEvent(1, true);
        imgui_consumed = io.WantCaptureMouse;
        break;
    case WM_RBUTTONUP:
        io.AddMouseButtonEvent(1, false);
        imgui_consumed = io.WantCaptureMouse;
        break;
    case WM_MOUSEMOVE:
        io.AddMousePosEvent((float)GET_X_LPARAM(lparam), (float)GET_Y_LPARAM(lparam));
        imgui_consumed = io.WantCaptureMouse;
        break;
    case WM_MOUSEWHEEL:
        io.AddMouseWheelEvent(0.0f, GET_WHEEL_DELTA_WPARAM(wparam) / (float)WHEEL_DELTA);
        imgui_consumed = io.WantCaptureMouse;
        break;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        io.AddKeyEvent(ImGuiKey_None, true);
        imgui_consumed = io.WantCaptureKeyboard;
        break;
    case WM_KEYUP:
    case WM_SYSKEYUP:
        io.AddKeyEvent(ImGuiKey_None, false);
        imgui_consumed = io.WantCaptureKeyboard;
        break;
    case WM_CHAR:
        io.AddInputCharacter((unsigned int)wparam);
        imgui_consumed = io.WantTextInput;
        break;
    }

    return imgui_consumed; // stop here, ImGui will handle it
}

bool& IgnoreInput = Make_Global<bool>(0x007E493C);

//
DECLARE_PATCH(_ScrollClass_Message_Handle_ImGui_Intercept)
{
    GET_STACK_STATIC(UINT*, message, esp, 0x2C);
    GET_STACK_STATIC(WPARAM*, wparam, esp, 0x30);
    GET_STACK_STATIC(LPARAM*, lparam, esp, 0x34);

    //_asm pushad

    if (!IgnoreInput || *message == WM_CAPTURECHANGED) {
        if (Handle_ImGui_Message(*message, *lparam, *wparam)) {
            JMP_REG(ecx, 0x005E961E);
        }
        _asm mov eax, message
        JMP_REG(ecx, 0x005E9373);
    }
    JMP_REG(ecx, 0x005E961E);
}


/**
 *  Main function for patching the hooks.
 */
void ScrollClassExtension_Hooks()
{
	Patch_Jump(0x005E8840, &_ScrollClass_Input_Allied_Cloaked_Object_Patch1);
	Patch_Jump(0x005E88AA, &_ScrollClass_Input_Allied_Cloaked_Object_Patch2);
    Patch_Jump(0x006167E3, &_Tactical_Get_Object_At_Cell_Allied_Cloaked_Object_Patch);
    Patch_Jump(0x005E935A, &_ScrollClass_Message_Handle_ImGui_Intercept);
}
