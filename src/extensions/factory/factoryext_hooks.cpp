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
#include "technotype.h"

#include "hooker.h"
#include "hooker_macros.h"
#include "mouse.h"
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
};


void FactoryClassFake::_Verify_Can_Build()
{
	const TechnoClass* producing_object = Get_Object();

	if (producing_object == nullptr)
		return;

	const TechnoTypeClass* producing_type = producing_object->Techno_Type_Class();

	if (producing_type == nullptr)
		return;

	if (!House->Can_Build(producing_type, false, false))
	{
		Abandon();

		if (House == PlayerPtr)
		{
			const RTTIType type = producing_type->Kind_Of();
			const int column = type == RTTI_BUILDING || type == RTTI_BUILDINGTYPE ? 0 : 1;
			Map.Column[column].Flag_To_Redraw();

			if (type == RTTI_BUILDING || type == RTTI_BUILDINGTYPE)
			{
				Map.PendingObject = nullptr;
				Map.PendingObjectPtr = nullptr;
				Map.PendingHouse = HOUSE_NONE;
				Map.Set_Cursor_Shape(nullptr);
			}
		}
	}

	for (int i = 0; i < QueuedObjects.Count(); i++)
	{
		if (!House->Can_Build(QueuedObjects[i], false, false))
		{
			Remove_From_Queue(*QueuedObjects[i]);
			i--;
		}
	}

    Resume_Queue();
}


/**
 *  Patch for InstantBuildCommandClass
 * 
 *  @author: CCHyper
 */
DECLARE_PATCH(_FactoryClass_AI_InstantBuild_Patch)
{
	GET_REGISTER_STATIC(FactoryClass *, this_ptr, esi);

	if (Vinifera_DeveloperMode) {

		/**
		 *  If AIInstantBuild is toggled on, make sure this is a non-human AI house.
		 */
		if (Vinifera_Developer_AIInstantBuild
			&& !this_ptr->House->Is_Human_Control() && this_ptr->House != PlayerPtr) {

			this_ptr->StageClass::Set_Stage(FactoryClass::STEP_COUNT);
			goto production_completed;
		}

		/**
		 *  If InstantBuild is toggled on, make sure the local player is a human house.
		 */
		if (Vinifera_Developer_InstantBuild
			&& this_ptr->House->Is_Human_Control() && this_ptr->House == PlayerPtr) {

			this_ptr->StageClass::Set_Stage(FactoryClass::STEP_COUNT);
			goto production_completed;
		}

		/**
		 *  If the AI has taken control of the player house, it needs a special
		 *  case to handle the "player" instant build mode.
		 */
		if (Vinifera_Developer_InstantBuild) {
			if (Vinifera_Developer_AIControl && this_ptr->House == PlayerPtr) {

				this_ptr->StageClass::Set_Stage(FactoryClass::STEP_COUNT);
				goto production_completed;
			}
		}

	}

	/**
	 *  Stolen bytes/code.
	 */
	if (this_ptr->StageClass::Fetch_Stage() == FactoryClass::STEP_COUNT) {
		goto production_completed;
	}

function_return:
	JMP(0x00496FA3);

    /**
     *  Production Completed, then suspend further production.
     */
production_completed:
	JMP(0x00496F73);
}


/**
 *  Update the queue and remove any items that are no
 *  longer buildable by the factory owner's house
 *
 *  @author: ZivDero
 */
DECLARE_PATCH(_Factory_Class_AI_Abandon_If_Cant_Build)
{
	GET_REGISTER_STATIC(FactoryClassFake*, this_ptr, ecx);

    _asm push esi

	this_ptr->_Verify_Can_Build();

	_asm
	{
		pop esi
		mov al, [esi + 5Ch]
		test al, al
	}

	JMP_REG(ebx, 0x00496EAC);
}


/**
 *  Main function for patching the hooks.
 */
void FactoryClassExtension_Hooks()
{
	Patch_Jump(0x00496F6D, &_FactoryClass_AI_InstantBuild_Patch);
	Patch_Jump(0x00496EA7, &_Factory_Class_AI_Abandon_If_Cant_Build);
}
