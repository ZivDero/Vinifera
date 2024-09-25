/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *
 *  @project       Vinifera
 *
 *  @file          HOUSEEXT_HOOKS.CPP
 *
 *  @author        CCHyper
 *
 *  @brief         Contains the hooks for the extended HouseClass.
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
#include "houseext_hooks.h"
#include "houseext_init.h"
#include "vinifera_globals.h"
#include "tibsun_globals.h"
#include "house.h"
#include "housetype.h"
#include "technotype.h"
#include "super.h"
#include "factory.h"
#include "techno.h"
#include "mouse.h"
#include "fatal.h"
#include "debughandler.h"
#include "asserthandler.h"

#include "hooker.h"
#include "hooker_macros.h"
#include "tibsun_functions.h"


/**
  *  A fake class for implementing new member functions which allow
  *  access to the "this" pointer of the intended class.
  *
  *  @note: This must not contain a constructor or deconstructor!
  *  @note: All functions must be prefixed with "_" to prevent accidental virtualization.
  */
static class HouseClassFake final : public HouseClass
{
public:
    ProdFailType _Begin_Production(RTTIType type, int id, bool resume);
    ProdFailType _Abandon_Production(RTTIType type, int id);
};


/**
 *  Reimplements the entire HouseClass::Begin_Production function.
 *
 *  @author: ZivDero
 */
ProdFailType HouseClassFake::_Begin_Production(RTTIType type, int id, bool resume)
{
    bool has_suspended = false;
    bool suspend = false;
    FactoryClass* fptr;
    TechnoTypeClass const* tech = Fetch_Techno_Type(type, id);

    if (!tech->Who_Can_Build_Me(false, true, true, this))
    {
        if (!resume || !tech->Who_Can_Build_Me(true, false, true, this))
        {
            DEBUG_INFO("Request to Begin_Production of '%s' was rejected. No-one can build.\n", tech->FullName);
            return PROD_CANT;
        }
        suspend = true;
    }

    fptr = Fetch_Factory(type);

    /*
    **	If no factory exists, create one.
    */
    if (fptr == nullptr)
    {
        fptr = new FactoryClass();
        if (!fptr)
        {
            DEBUG_INFO("Request to Begin_Production of '%s' was rejected. Unable to create factory\n", tech->FullName);
            return PROD_CANT;
        }
        Set_Factory(type, fptr);
    }

    /*
    **	If the house is already busy producing a building, then
    **	return with this failure code.
    */
    if (fptr->Is_Building() && type == RTTI_BUILDINGTYPE)
    {
        DEBUG_INFO("Request to Begin_Production of '%s' was rejected. Cannot queue buildings.\n", tech->FullName);
        return PROD_CANT;
    }

    /*
    **	Check if we have an object of this type currently suspended in production.
    */
    if (fptr->IsSuspended)
    {
        ObjectClass* obj = fptr->Get_Object();
        if (obj != nullptr && obj->Techno_Type_Class() == tech)
        {
            has_suspended = true;
        }
    }

    if (has_suspended || fptr->Set(*tech, *this, resume))
    {
        if (has_suspended || resume || fptr->Queued_Object_Count() == 0)
        {
            fptr->Start(suspend);

            /*
            **	Link this factory to the sidebar so that proper graphic feedback
            **	can take place.
            */
            if (PlayerPtr == this)
                Map.Factory_Link(fptr, type, id);

            return PROD_OK;
        }
        else
        {
            SidebarExtension->Get_Tab(type).Flag_To_Redraw();
            return PROD_OK;
        }
    }

    DEBUG_INFO("Request to Begin_Production of '%s' was rejected. Factory was unable to create the requested object\n", tech->Full_Name());

    /*
    **	If the factory has queued objects or is currently
    **  building an object, reject production.
    */
    if (fptr->Queued_Object_Count() > 0 || fptr->Object)
        return PROD_CANT;


    /*
    **	Output debug information if production failed.
    */
    DEBUG_INFO("type=%d\n", type);
    DEBUG_INFO("Frame == %d\n", Frame);
    DEBUG_INFO("fptr->QueuedObjects.Count() == %d\n", fptr->QueuedObjects.Count());
    if (fptr->Get_Object())
    {
        DEBUG_INFO("Object->RTTI == %d\n", fptr->Object->Kind_Of());
        DEBUG_INFO("Object->HeapID == %d\n", fptr->Object->Get_Heap_ID());
    }
    DEBUG_INFO("IsSuspended\t= %d\n", fptr->IsSuspended);

    delete fptr;
    Set_Factory(type, nullptr);

    return PROD_CANT;
}


/**
 *  Reimplements the entire HouseClass::Abandon_Production function.
 *
 *  @author: ZivDero
 */
ProdFailType HouseClassFake::_Abandon_Production(RTTIType type, int id)
{
    FactoryClass* fptr = Fetch_Factory(type);

    /*
    **	If there is no factory to abandon, then return with a failure code.
    */
    if (fptr == nullptr)
        return PROD_CANT;

    /*
    **	If we're just dequeuing a unit, redraw the strip.
    */
    if (fptr->Queued_Object_Count() > 0 && id >= 0)
    {
        const TechnoTypeClass* technotype = Fetch_Techno_Type(type, id);
        if (fptr->Remove_From_Queue(*technotype))
        {
            SidebarExtension->Current_Tab().Flag_To_Redraw();
            return PROD_OK;
        }
    }

    if (id != -1)
    {
        ObjectClass* obj = fptr->Get_Object();
        if (obj == nullptr)
            return PROD_OK;

        ObjectTypeClass* cls = obj->Class_Of();
        if (id != cls->Get_Heap_ID())
            return PROD_OK;
    }

    /*
    **	Tell the sidebar that it needs to be redrawn because of this.
    */
    if (PlayerPtr == this)
    {
        Map.Abandon_Production(type, fptr);
        if (type == RTTI_BUILDINGTYPE || type == RTTI_BUILDING)
        {
            Map.PendingObjectPtr = nullptr;
            Map.PendingObject = nullptr;
            Map.PendingHouse = HOUSE_NONE;
            Map.Set_Cursor_Shape(nullptr);
        }
    }

    /*
    **	Abandon production of the object.
    */
    fptr->Abandon();
    if (fptr->Queued_Object_Count() > 0)
    {
        fptr->Resume_Queue();
        return PROD_OK;
    }

    Set_Factory(type, nullptr);
    delete fptr;

    return PROD_OK;
}


/**
 *  Patch for InstantSuperRechargeCommandClass
 * 
 *  @author: CCHyper
 */
DECLARE_PATCH(_HouseClass_Super_Weapon_Handler_InstantRecharge_Patch)
{
    GET_REGISTER_STATIC(HouseClass *, this_ptr, edi);
    GET_REGISTER_STATIC(SuperClass *, special, esi);
    static bool is_player;

    is_player = false;
    if (this_ptr == PlayerPtr) {
        is_player = true;
    }

    if (Vinifera_DeveloperMode) {

        if (!special->IsReady) {

            /**
             *  If AIInstantBuild is toggled on, make sure this is a non-human AI house.
             */
            if (Vinifera_Developer_AIInstantSuperRecharge
                && !this_ptr->Is_Human_Control() && this_ptr != PlayerPtr) {

                special->Forced_Charge(is_player);

            /**
             *  If InstantBuild is toggled on, make sure the local player is a human house.
             */
            } else if (Vinifera_Developer_InstantSuperRecharge
                && this_ptr->Is_Human_Control() && this_ptr == PlayerPtr) {
                
                special->Forced_Charge(is_player);

            /**
             *  If the AI has taken control of the player house, it needs a special
             *  case to handle the "player" instant recharge mode.
             */
            } else if (Vinifera_Developer_InstantSuperRecharge) {
                if (Vinifera_Developer_AIControl && this_ptr == PlayerPtr) {
                    
                    special->Forced_Charge(is_player);
                }
            }

        }

    }

    /**
     *  Stolen bytes/code.
     */
    if (!special->AI(is_player)) {
        goto continue_function;
    }

add_to_sidebar:
    JMP(0x004BD320);

continue_function:
    JMP(0x004BD332);
}


/**
 *  Patch for BuildCheatCommandClass
 * 
 *  @author: CCHyper
 */
DECLARE_PATCH(_HouseClass_Can_Build_BuildCheat_Patch)
{
    GET_REGISTER_STATIC(HouseClass *, this_ptr, ebp);
    GET_REGISTER_STATIC(int, vector_count, ecx);
    GET_STACK_STATIC(TechnoTypeClass *, objecttype, esp, 0x30);

    if (Vinifera_DeveloperMode && Vinifera_Developer_BuildCheat) {

        /**
         *  AI houses have access to everything, so we can just
         *  filter to the human houses only.
         */
        if (this_ptr->IsHuman && this_ptr->IsPlayerControl) {

            /**
             *  Check that the object has this house set as one of its owners.
             *  if true, force this 
             */
            if (((1 << this_ptr->Class->ID) & objecttype->Get_Ownable()) != 0) {
                //DEBUG_INFO("Forcing \"%s\" available.\n", objecttype->IniName);
                goto return_true;
            }
        }
    }

    /**
     *  Stolen bytes/code.
     */
original_code:
    _asm { xor eax, eax }
    _asm { mov [esp+0x34], eax }

    _asm { mov ecx, vector_count }
    _asm { test ecx, ecx }

    JMP_REG(ecx, 0x004BBD2E); // Need to use ECX as EAX is used later on.

return_true:
    JMP(0x004BBD17);
}


/**
 *  Main function for patching the hooks.
 */
void HouseClassExtension_Hooks()
{
    /**
     *  Initialises the extended class.
     */
    HouseClassExtension_Init();

    Patch_Jump(0x004BBD26, &_HouseClass_Can_Build_BuildCheat_Patch);
    Patch_Jump(0x004BD30B, &_HouseClass_Super_Weapon_Handler_InstantRecharge_Patch);

    Patch_Jump(0x004BE200, &HouseClassFake::_Begin_Production);
    Patch_Jump(0x004BE6A0, &HouseClassFake::_Abandon_Production);
}
