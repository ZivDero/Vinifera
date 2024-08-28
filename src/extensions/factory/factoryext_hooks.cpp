/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *
 *  @project       Vinifera
 *
 *  @file          FACTORYEXT_HOOKS.CPP
 *
 *  @author        CCHyper
 *
 *  @brief         Contains the hooks for the extended FactoryClass.
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
#include "factoryext_hooks.h"
#include "tibsun_globals.h"
#include "vinifera_globals.h"
#include "house.h"
#include "factory.h"
#include "fatal.h"
#include "debughandler.h"
#include "asserthandler.h"
#include "extension_globals.h"
#include "technotype.h"

#include "hooker.h"
#include "hooker_macros.h"
#include "mouse.h"
#include "sidebarext.h"
#include "techno.h"


 /**
  *  A fake class for implementing new member functions which allow
  *  access to the "this" pointer of the intended class.
  *
  *  @note: This must not contain a constructor or deconstructor!
  *  @note: All functions must be prefixed with "_" to prevent accidental virtualization.
  */
static class FactoryClassFake final : public FactoryClass
{
public:
    void _Verify_Can_Build();
    void _AI();
};


/**
 *  Checks if this factory should abandon construction because the objects
 *  in the queue are no longer available to build
 *
 *  @author: ZivDero
 */
void FactoryClassFake::_Verify_Can_Build()
{
    const TechnoClass* producing_object = Get_Object();

    if (producing_object == nullptr)
        return;

    const TechnoTypeClass* producing_type = producing_object->Techno_Type_Class();
    const RTTIType type = producing_type->Kind_Of();
    const bool is_building = type == RTTI_BUILDING || type == RTTI_BUILDINGTYPE;

    bool need_update = false;

    // Check the thing we're currently building separately - it needs special handling
    if (!House->Can_Build(producing_type, false, true))
    {
        Abandon();
        need_update = true;

        // Remove map placement if we're doing that
        if (is_building && House == PlayerPtr)
        {
            Map.PendingObject = nullptr;
            Map.PendingObjectPtr = nullptr;
            Map.PendingHouse = HOUSE_NONE;
            Map.Set_Cursor_Shape(nullptr);
        }
    }

    // Now make sure there are no invalid objects in the queue
    for (int i = 0; i < QueuedObjects.Count(); i++)
    {
        if (!House->Can_Build(QueuedObjects[i], false, true))
        {
            Remove_From_Queue(*QueuedObjects[i]);
            need_update = true;
            i--;
        }
    }

    if (need_update)
    {
        if (House == PlayerPtr)
            SidebarExtension->Get_Tab(type).Flag_To_Redraw();

        House->Update_Factories(type);
        Resume_Queue();
    }
}


void FactoryClassFake::_AI()
{
    _Verify_Can_Build();

    if (!IsSuspended && (Object != nullptr || SpecialItem))
    {
        for (int index = 0; index < 1; index++)
        {
            if (!Has_Completed() && Graphic_Logic())
            {
                IsDifferent = true;

                int cost = Cost_Per_Tick();

                cost = std::min(cost, Balance);

                /*
                **	Enough time has expired so that another production step can occur.
                **	If there is insufficient funds, then go back one production step and
                **	continue the countdown. The idea being that by the time the next
                **	production step occurs, there may be sufficient funds available.
                */
                if (cost > House->Available_Money())
                {
                    Set_Stage(Fetch_Stage() - 1);
                }
                else
                {
                    House->Spend_Money(cost);
                    Balance -= cost;
                }

                if (Vinifera_DeveloperMode)
                {
                    /*
                    **	If AIInstantBuild is toggled on, make sure this is a non-human AI house.
                    */
                    if (Vinifera_Developer_AIInstantBuild
                        && !House->Is_Human_Control() && House != PlayerPtr)
                    {
                        Set_Stage(STEP_COUNT);
                    }

                    /*
                    **	If InstantBuild is toggled on, make sure the local player is a human house.
                    */
                    if (Vinifera_Developer_InstantBuild
                        && House->Is_Human_Control() && House == PlayerPtr)
                    {
                        Set_Stage(STEP_COUNT);
                    }

                    /*
                    **	If the AI has taken control of the player house, it needs a special
                    **	case to handle the "player" instant build mode.
                    */
                    if (Vinifera_Developer_InstantBuild)
                    {
                        if (Vinifera_Developer_AIControl && House == PlayerPtr)
                            Set_Stage(STEP_COUNT);
                    }

                }

                /*
                **	If the production has completed, then suspend further production.
                */
                if (Fetch_Stage() == STEP_COUNT)
                {
                    IsSuspended = true;
                    Set_Rate(0);
                    House->Spend_Money(Balance);
                    Balance = 0;
                }
            }
        }
    }
}


/**
 *  Main function for patching the hooks.
 */
void FactoryClassExtension_Hooks()
{
    Patch_Jump(0x00496EA0, &FactoryClassFake::_AI);
}
