/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *
 *  @project       Vinifera
 *
 *  @file          CRT_HOOKS.CPP
 *
 *  @author        CCHyper
 *
 *  @brief         Setup all the hooks to take control of the basic CRT.
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
#include "crt_hooks.h"
#include <fenv.h>
#include "vinifera_newdel.h"
#include "tibsun_globals.h"
#include "session.h"
#include "asserthandler.h"
#include "debughandler.h"

#include "hooker.h"
#include "hooker_macros.h"


/**
 *  Redirect msize() to use HeapSize as we now control all memory allocations.
 */
static unsigned int __cdecl vinifera_msize(void *ptr)
{
    return HeapSize(GetProcessHeap(), 0, ptr);
}


/**
 *  Reimplementation of strdup() to use our allocator.
 */
static char * __cdecl vinifera_strdup(const char *string)
{
    char *str;
    char *p;
    int len = 0;

    while (string[len]) {
        len++;
    }
    str = (char *)vinifera_allocate(len + 1);
    p = str;
    while (*string) {
        *p++ = *string++;
    }
    *p = '\0';
    return str;
}


static void Set_Vinifera_FP_Mode()
{
    _set_controlfp(_PC_24, _MCW_PC);
    _set_controlfp(_RC_CHOP, _MCW_RC); // _RC_NEAR in SupCom code
}


/**
 *  Set the FPU mode to match the game (rounding towards zero [chop mode]).
 */
DECLARE_PATCH(_set_fp_mode)
{
    // Call to "WWDebug_Printf"
    _asm { mov edx, 0x004082D0 }
    _asm { call edx }

    /**
     *  Set the FPU mode.
     *  According to a Supreme Commander developer, this mode is
     *  necessary for determinism.
     *  https://gafferongames.com/post/floating_point_determinism/
     */
    Set_Vinifera_FP_Mode();

    // Call the game's function to store the FPU mode.
    _asm { mov edx, 0x006B2314 }
    _asm { call edx }

    /**
     *  And this is required for the std c++ lib.
     */
    fesetround(FE_TONEAREST);

    JMP(0x005FFDB0);
}


static void Check_Vinifera_FP_Mode()
{
    // Fetch FP control value
    int fpcontrol = _controlfp(0, 0);
    if ((fpcontrol & _MCW_PC) != _PC_24)
    {
        DEBUG_FATAL("FPU precision mode change detected. Value: 0x%08x\n", fpcontrol);
        Emergency_Exit(255);
    }

    if ((fpcontrol & _MCW_RC) != _RC_CHOP) // _RC_NEAR
    {
        DEBUG_FATAL("FPU rounding mode change detected. Value: 0x%08x\n", fpcontrol);
        Emergency_Exit(255);
    }
}

DECLARE_PATCH(_LogicClass_AI_Beginning_Set_FP_Mode)
{
    _asm { push  ecx }

    Set_Vinifera_FP_Mode();

    // Stolen bytes / code
    _asm { mov  edx, dword ptr ds:0x00804D28 } // mov edx, LogicInit
    _asm { pop  ecx }
    JMP(0x00506AB9);
}


DECLARE_PATCH(_LogicClass_AI_End_Check_FP_Mode)
{
    Check_Vinifera_FP_Mode();

    // Rebuild function epilogue
    _asm { add  esp, 28h }
    _asm { retn }
}


/**
 *  Main function for patching the hooks.
 */
void CRT_Hooks()
{
    /**
     *  Call the games fpmath to make sure we init 
     */
    Patch_Jump(0x005FFD97, &_set_fp_mode);

    // Set the FP mode in the beginning of LogicClass::AI
    Patch_Jump(0x00506AB3, &_LogicClass_AI_Beginning_Set_FP_Mode);
    Patch_Jump(0x00507205, &_LogicClass_AI_End_Check_FP_Mode);

    /**
     *  dynamic init functions call _msize indirectly.
     *  They call __onexit, so we need to patch this.
     */
    Hook_Function(0x006B80AA, &vinifera_msize);

    /**
     *  Standard functions.
     */
    Hook_Function(0x006BE766, &vinifera_strdup);

    /**
     *  C memory functions.
     */
    Hook_Function(0x006B72CC, &vinifera_allocate);
    Hook_Function(0x006BCA26, &vinifera_count_allocate);
    Hook_Function(0x006B7F72, &vinifera_reallocate);
    Hook_Function(0x006B67E4, &vinifera_free);

    /**
     *  C++ new and delete.
     */
    Hook_Function(0x006B51D7, &vinifera_allocate);
    Hook_Function(0x006B51CC, &vinifera_free);
}
