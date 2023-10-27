/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *
 *  @project       Vinifera
 *
 *  @file          BUILDINGTYPEEXT_HOOKS.CPP
 *
 *  @author        CCHyper
 *
 *  @brief         Contains the hooks for the extended BuildingTypeClass.
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
#include "buildingtypeext_hooks.h"
#include "buildingtypeext_init.h"
#include "buildingtypeext.h"
#include "buildingtype.h"
#include "fatal.h"
#include "debughandler.h"
#include "asserthandler.h"

#include "hooker.h"
#include "hooker_macros.h"


 /**
  *  A fake class for implementing new member functions which allow
  *  access to the "this" pointer of the intended class.
  *
  *  @note: This must not contain a constructor or deconstructor!
  *  @note: All functions must be prefixed with "_" to prevent accidental virtualization.
  */
static class BuildingTypeClassFake final : public BuildingTypeClass
{
public:
    int _Raw_Cost();
    int _Cost_Of(HouseClass* house);
};


/**
 *  Patches in an assertion check for image data.
 * 
 *  @author: CCHyper
 */
DECLARE_PATCH(_BuildingTypeClass_Get_Image_Data_Assertion_Patch)
{
    GET_REGISTER_STATIC(BuildingTypeClass *, this_ptr, esi);
    GET_REGISTER_STATIC(const ShapeFileStruct *, image, eax);

    if (image == nullptr) {
        DEBUG_WARNING("Building %s has NULL image data!\n", this_ptr->Name());
    }

    _asm { mov eax, image } // restore eax state.
    _asm { pop esi }
    _asm { add esp, 0x64 }
    _asm { ret }
}


/**
 *  Disables weird Westwood logic that links a BuildingType's cost to the cost
 *  of its FreeUnit or pad aircraft.
 * 
 *  Author: Rampastring
 */
int BuildingTypeClassFake::_Raw_Cost()
{
    return TechnoTypeClass::Raw_Cost();
}


/**
 *  Disables weird Westwood logic that links a BuildingType's cost to the cost
 *  of its FreeUnit or pad aircraft.
 *
 *  Author: Rampastring
 */
int BuildingTypeClassFake::_Cost_Of(HouseClass* house)
{
    return TechnoTypeClass::Cost_Of(house);
}

/**
 *  Main function for patching the hooks.
 */
void BuildingTypeClassExtension_Hooks()
{
    /**
     *  Initialises the extended class.
     */
    BuildingTypeClassExtension_Init();

    //Patch_Jump(0x00440365, &_BuildingTypeClass_Get_Image_Data_Assertion_Patch);

    Patch_Jump(0x00440000, &BuildingTypeClassFake::_Raw_Cost);
    Patch_Jump(0x00440080, &BuildingTypeClassFake::_Cost_Of);
}
