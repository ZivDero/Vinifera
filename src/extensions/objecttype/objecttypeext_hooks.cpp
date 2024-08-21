/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *
 *  @project       Vinifera
 *
 *  @file          OBJECTTYPEEXT_HOOKS.CPP
 *
 *  @author        CCHyper
 *
 *  @brief         Contains the hooks for the extended ObjectTypeClass.
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
#include "objecttypeext_hooks.h"
#include "objecttypeext.h"
#include "objecttype.h"
#include "building.h"
#include "buildingtype.h"
#include "unittype.h"
#include "unittypeext.h"
#include "theatertype.h"
#include "vinifera_globals.h"
#include "tibsun_globals.h"
#include "house.h"
#include "housetype.h"
#include "scenario.h"
#include "extension.h"
#include "wstring.h"
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
static class ObjectTypeClassExt final : public ObjectTypeClass
{
    public:
        void _Assign_Theater_Name(char *buffer, TheaterType theater);
        const ShapeFileStruct * _Get_Image_Data() const;
};


/**
 *  Reimplementation of ObjectTypeClass::Assign_Theater_Name to support new theater types.
 * 
 *  @author: CCHyper
 */
void ObjectTypeClassExt::_Assign_Theater_Name(char *fname, TheaterType theater)
{
    /**
     *  Make sure filename is uppercase.
     */
    strupr(fname);

    /**
     *  An edge case we exposed in the original game where it assumed anything that
     *  matched the pattern (e.g. GACNST) would also be marked with "IsNewTheater".
     *  Now that we support custom theaters, this means that might no longer be
     *  the case, so we perform a check before we perform the filename theater remap
     *  so the filename is left unmodified.
     */
    if (!IsNewTheater) return;

    /**
     *  Another edge case we have exposed in the original where some civilian buildings were
     *  marked with "IsNewTheater", but did not follow the theater filename system. These
     *  were most likely early additions into the game development when it was still using
     *  the Red Alert filename format. Unfortunately, the only way we can resolve this is
     *  to hard code checks for this filename prefixes and skip any remap attempt.
     */
    if (What_Am_I() == RTTI_BUILDINGTYPE && (!std::strncmp(fname, "CITY", 4) || !std::strncmp(fname, "ABAN", 4) || !std::strncmp(fname, "BBOARD", 5))) {
        DEV_DEBUG_WARNING("Skipping new theater filename remap of %s!\n", fname);
        return;
    }

    /**
     *  Same as above, but for the deployed mobile war factory, cabal obelisk, and their
     *  respective animations.
     */
    if (What_Am_I() == RTTI_BUILDINGTYPE && (std::strstr(fname, "MWAR") || std::strstr(fname, "OBL1"))) {
        DEV_DEBUG_WARNING("Skipping new theater filename remap of %s!\n", fname);
        return;
    }

    if (theater != THEATER_NONE && theater < TheaterTypes.Count()) {

        char first = fname[0];
        char second = fname[1];

        /**
         *  Remap the second character to the current theater image character. We perform
         *  a simple check to make sure the characters are valid.
         */
        if (std::isalpha(first) && std::isalpha(second)) {
            fname[0] = first;
            fname[1] = TheaterTypeClass::ImageLetter_From(theater);

        } else {
            DEV_DEBUG_WARNING("Failed to remap filename \"%s\" to current theater (%s)!\n", fname, TheaterTypeClass::Name_From(theater));
        }
    }
}


/**
 *  This patch replaces an inlined instance of ObjectTypeClass::Assign_Theater_Name
 *  with a direct call.
 * 
 *  @author: CCHyper
 */
DECLARE_PATCH(_ObjectTypeClass_Load_Theater_Art_Assign_Theater_Name_Theater_Patch)
{
    GET_REGISTER_STATIC(ObjectTypeClass *, this_ptr, edi);
    LEA_STACK_STATIC(char *, fullname, esp, 0x0C);
    LEA_STACK_STATIC(char *, destbuffer, esp, 0x08);

    this_ptr->Assign_Theater_Name(fullname, Scen->Theater);

    JMP(0x005889E2);
}


/**
 *  Reimplementation of ObjectTypeClass::Get_Image_Data with added assertion.
 * 
 *  @author: CCHyper
 */
const ShapeFileStruct * ObjectTypeClassExt::_Get_Image_Data() const
{
    if (Image == nullptr) {
        DEBUG_WARNING("Object %s has NULL image data!\n", Name());
    }

    return Image;
}


/**
 *  Prevents regular war factories from building ships and naval yards
 *  from building vehicles.
 *
 *  @author: Rampastring
 */
DECLARE_PATCH(_ObjectTypeClass__Who_Can_Build_Me_Naval_Yard_Patch)
{
    GET_REGISTER_STATIC(BuildingClass*, building, esi);
    GET_REGISTER_STATIC(ObjectTypeClass*, this_ptr, edi);
    static UnitTypeClass* unittype;
    static UnitTypeClassExtension* unittypeext;

    /**
     *  Stolen bytes / code.
     *  If the building is being sold, skip it.
     */
    if (building->Get_Mission() == MISSION_DECONSTRUCTION || building->MissionQueue == MISSION_DECONSTRUCTION) {
        goto skip_building;
    }

    /**
     *  Check if we are a vehicle (UnitType).
     *  If so, check if we are a naval unit.
     *
     *  If yes, we should only allow us to be built from buildings that are water-bound.
     *
     *  If we are instead a non-naval vehicle, then do not allow building
     *  us from naval-bound buildings.
     */
    if (this_ptr->What_Am_I() == RTTI_UNITTYPE) {

        unittype = reinterpret_cast<UnitTypeClass*>(this_ptr);
        unittypeext = Extension::Fetch<UnitTypeClassExtension>(unittype);

        if (unittypeext->IsNaval) {
            if (building->Class->Speed != SPEED_FLOAT) {
                goto skip_building;
            }
        } else {
            if (building->Class->Speed == SPEED_FLOAT) {
                goto skip_building;
            }
        }
    }

    /**
     *  Allow the building through our checks and
     *  continue the original game's legality checks.
     */
continue_checks:
    JMP(0x00587BB9);

    /**
     *  Go to the next building in the loop.
     */
skip_building:
    JMP(0x00587C46);
}


/**
 *  Main function for patching the hooks.
 */
void ObjectTypeClassExtension_Hooks()
{
    //Patch_Jump(0x004101A0, &ObjectTypeClassExt::_Get_Image_Data);
    Patch_Jump(0x00588D00, &ObjectTypeClassExt::_Assign_Theater_Name);
    Patch_Jump(0x0058891D, &_ObjectTypeClass_Load_Theater_Art_Assign_Theater_Name_Theater_Patch);
    Patch_Jump(0x00587B9C, &_ObjectTypeClass__Who_Can_Build_Me_Naval_Yard_Patch);
    Patch_Jump(0x00587BFA, 0x00587C0B); // Skip checking the owner of the MCV when building buildings in ObjectTypeClass::Who_Can_Build_Me
}
