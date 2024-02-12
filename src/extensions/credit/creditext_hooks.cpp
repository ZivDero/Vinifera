/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *
 *  @project       Vinifera
 *
 *  @file          CREDITEXT_HOOKS.CPP
 *
 *  @author        Rampastring
 *
 *  @brief         Contains the hooks for the extended CreditClass.
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

#include "tibsun_globals.h"

#include "colorscheme.h"
#include "dsurface.h"
#include "extension_globals.h"
#include "house.h"
#include "housetype.h"
#include "rgb.h"
#include "scenarioext.h"


#include "fatal.h"
#include "debughandler.h"
#include "asserthandler.h"

#include "hooker.h"
#include "hooker_macros.h"

/**
 *  Helper function for getting a ColorScheme instance
 *  based on the player's side. Necessary because the switch-case
 *  trashes the stack.
 */
ColorScheme* UI_Color_Scheme_From_Player_House()
{
    // Allow overriding the UI color with a special scenario-defined color.
    if (ScenExtension != nullptr) {
        if (ScenExtension->UIColorOverrideName[0] != '\0') {
            return ColorScheme::As_Pointer(ScenExtension->UIColorOverrideName);
        }
    }

    if (PlayerPtr != nullptr) 
    {
        switch (PlayerPtr->ActLike)
        {
            case 0: // GDI
            case 1: // Nod
                return ColorScheme::As_Pointer("LightGold");
                break;
            case 2: // Allies
                return ColorScheme::As_Pointer("White");
                break;
            case 3: // Soviet
                return ColorScheme::As_Pointer("White");
                break;
        }
    }

    return nullptr;
}


/**
 *  Helper function for getting a ColorScheme instance
 *  based on the player's side. Necessary because the switch-case
 *  trashes the stack.
 */
ColorScheme* ToolTip_Color_Scheme_From_Player_House()
{
    // Allow overriding the UI color with a special scenario-defined color.
    if (ScenExtension != nullptr) {
        if (ScenExtension->UIColorOverrideName[0] != '\0') {
            return ColorScheme::As_Pointer(ScenExtension->UIColorOverrideName);
        }
    }

    if (PlayerPtr != nullptr)
    {
        switch (PlayerPtr->ActLike)
        {
        case 0: // GDI
        case 1: // Nod
            return ColorScheme::As_Pointer("LightGold");
            break;
        case 2: // Allies
            return ColorScheme::As_Pointer("Gold");
            break;
        case 3: // Soviet
            return ColorScheme::As_Pointer("Gold");
            break;
        }
    }

    return nullptr;
}


void Fetch_UI_Color_Scheme()
{
    // If the color scheme is null, fetch it.
    if (ScenExtension != nullptr && ScenExtension->CachedUIColorSchemeIndex < 0) {

        ColorScheme* colorscheme = UI_Color_Scheme_From_Player_House();

        // If we could not find a color scheme based on the player house, then default to the first color in the list.
        if (colorscheme != nullptr) {
            ScenExtension->CachedUIColorSchemeIndex = colorscheme->ID - 1;
        } else {
            ScenExtension->CachedUIColorSchemeIndex = 0;
        }
    }

    // If the ToolTip color scheme is null, fetch it.
    if (ScenExtension != nullptr && ScenExtension->CachedToolTipColorSchemeIndex < 0) {
        ColorScheme* colorscheme = ToolTip_Color_Scheme_From_Player_House();

        // If we could not find a color scheme based on the player house, then default to the first color in the list.
        if (colorscheme != nullptr) {
            ScenExtension->CachedToolTipColorSchemeIndex = colorscheme->ID - 1;
        }
        else {
            ScenExtension->CachedToolTipColorSchemeIndex = 0;
        }
    }
}


/**
 *  Modifies the color of the "Options" text based on the player's side.
 */
DECLARE_PATCH(_TabClass_Draw_It_Faction_Specific_Options_Button_Color_Scheme_Patch)
{
    Fetch_UI_Color_Scheme();
    static ColorScheme* colorscheme;

    colorscheme = ColorSchemes[ScenExtension->CachedUIColorSchemeIndex];

    _asm { mov edx, dword ptr ds : colorscheme }
    _asm { mov ecx, dword ptr ds : 0x0074C5E4 } // Restore TempSurface to ecx
    JMP(0x0060E5B4);
}


/**
 *  Modifies the color of the credits display based on the player's side.
 */
DECLARE_PATCH(_CreditClass_Graphic_Logic_Faction_Specific_Color_Scheme_Patch)
{
    _asm { push ecx }
    _asm { push 4108h }
    _asm { push 0 }

    Fetch_UI_Color_Scheme();

    static ColorScheme* colorscheme;

    colorscheme = ColorSchemes[ScenExtension->CachedUIColorSchemeIndex];

    _asm { mov eax, dword ptr ds : colorscheme }
    JMP_REG(ecx, 0x004714F0);
}


DECLARE_PATCH(_TechnoClass_Draw_Power_Level_Text_Faction_Specific_Color_Scheme_Patch)
{
    static int colorschemeindex;

    _asm { push 1 }

    _asm { push ecx }
    Fetch_UI_Color_Scheme();
    colorschemeindex = ScenExtension->CachedUIColorSchemeIndex;
    _asm { pop ecx }

    _asm { mov eax, dword ptr ds : colorschemeindex }
    _asm { push eax }
    _asm { push 149h }
    _asm { mov edx, dword ptr ds : 0x0074C5E4 } // Restore TempSurface to edx
    JMP(0x00637DE4);
}


DECLARE_PATCH(_TechnoClass_Draw_Primary_Text_Faction_Specific_Color_Scheme_Patch)
{
    static int colorschemeindex;

    _asm { push 1 }

    Fetch_UI_Color_Scheme();
    colorschemeindex = ScenExtension->CachedUIColorSchemeIndex;

    _asm { mov eax, dword ptr ds : colorschemeindex }
    _asm { push eax }
    _asm { push 149h }
    _asm { mov ecx, dword ptr ds : 0x0074C5E4 } // Restore TempSurface to ecx
    JMP(0x00637E33);
}


void Draw_Tooltip_Rectangle(DSurface* surface, Rect& drawrect)
{
    surface->Fill_Rect(drawrect, 0);

    if (ScenExtension->CachedToolTipColorSchemeIndex > -1) {
        RGBClass rgb = ColorSchemes[ScenExtension->CachedToolTipColorSchemeIndex]->field_308.operator RGBClass();
        surface->Draw_Rect(drawrect, DSurface::RGB_To_Pixel(rgb));
    }
    else {
        surface->Draw_Rect(drawrect, 0);
    }
}


DECLARE_PATCH(_CCToolTip_Draw_Faction_Specific_Color_Scheme_Rect_Patch)
{
    GET_REGISTER_STATIC(DSurface*, surface, esi);
    GET_REGISTER_STATIC(Rect*, drawrect, eax);

    Draw_Tooltip_Rectangle(surface, *drawrect);
    
    JMP(0x0044E6D4);
}


DECLARE_PATCH(_CCToolTip_Draw_Faction_Specific_Color_Scheme_Text_Patch)
{
    static const char* colorname;

    if (ScenExtension->CachedToolTipColorSchemeIndex > -1) {
        colorname = ColorSchemes[ScenExtension->CachedToolTipColorSchemeIndex]->Name;
    }
    else {
        colorname = "Green";
    }

    _asm { mov ecx, colorname }
    JMP_REG(eax, 0x0044E6E7);
}


static const char* loading_screen_color_name;
static bool loading_screen_color_name_set = false;

DECLARE_PATCH(_ProgressScreenClass_Draw_Faction_Specific_Color_Scheme_Patch) 
{
    static SideType side;

    if (!loading_screen_color_name_set) {

        side = (SideType)Scen->IsGDI;

        loading_screen_color_name = "Green";

        if (side == 0) {
            loading_screen_color_name = "Green";
        }
        else if (side == 1) {
            loading_screen_color_name = "Green";
        }
        else if (side == 2) {
            loading_screen_color_name = "White";
        }
        else if (side == 3) {
            loading_screen_color_name = "White";
        }

        loading_screen_color_name_set = true;
    }

    _asm { mov ecx, loading_screen_color_name };
    JMP(0x005ADF5F);
}


/**
 *  Main function for patching the hooks.
 */
void CreditClassExtension_Hooks()
{
    Patch_Jump(0x0060E5AE, &_TabClass_Draw_It_Faction_Specific_Options_Button_Color_Scheme_Patch);
    Patch_Jump(0x004714E6, &_CreditClass_Graphic_Logic_Faction_Specific_Color_Scheme_Patch);
     
    // The functions used below wouldn't be very hard to reimplement entirely...
    Patch_Jump(0x00637DDB, &_TechnoClass_Draw_Power_Level_Text_Faction_Specific_Color_Scheme_Patch);
    Patch_Jump(0x00637E2A, &_TechnoClass_Draw_Primary_Text_Faction_Specific_Color_Scheme_Patch);

    Patch_Jump(0x0044E682, &_CCToolTip_Draw_Faction_Specific_Color_Scheme_Rect_Patch);
    Patch_Jump(0x0044E6E2, &_CCToolTip_Draw_Faction_Specific_Color_Scheme_Text_Patch);

    Patch_Jump(0x005ADF5A, &_ProgressScreenClass_Draw_Faction_Specific_Color_Scheme_Patch);
}