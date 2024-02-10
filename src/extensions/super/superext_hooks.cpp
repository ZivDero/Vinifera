/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *
 *  @project       Vinifera
 *
 *  @file          SUPEREXT_HOOKS.CPP
 *
 *  @author        CCHyper
 *
 *  @brief         Contains the hooks for the extended SuperClass.
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
#include "superext_hooks.h"
#include "superext_init.h"
#include "superext.h"
#include "aircraft.h"
#include "aircraftext.h"
#include "aircrafttype.h"
#include "house.h"
#include "infantrytype.h"
#include "mouse.h"
#include "reinf.h"
#include "scenarioext.h"
#include "scripttype.h"
#include "taskforce.h"
#include "teamtype.h"
#include "fatal.h"
#include "asserthandler.h"
#include "debughandler.h"

#include "hooker.h"
#include "hooker_macros.h"


/**
 *  A fake class for implementing new member functions which allow
 *  access to the "this" pointer of the intended class.
 *
 *  @note: This must not contain a constructor or deconstructor!
 *  @note: All functions must be prefixed with "_" to prevent accidental virtualization.
 */
static class SuperClassFake final : public SuperClass
{
public:
    void _Do_DropPods(Cell* cell);
};


void SuperClassFake::_Do_DropPods(Cell* cell)
{
	char buffer[24];
	
	sprintf(buffer, "PARADROPINF_%d", this->House->ID);
	const TeamTypeClass* ttype = TeamTypeClass::As_Pointer(buffer);

	if (ttype == nullptr) {
		TeamTypeClass* newteamtype = new TeamTypeClass();

		if (newteamtype != nullptr) {
			strcpy(newteamtype->IniName, "PARADROPINF");
			// ttype->IsTransient = true;
			newteamtype->IsPrebuilt = false;
			newteamtype->IsReinforcable = false;
			newteamtype->IsSuicide = true;
			newteamtype->Origin = WAYPT_SPECIAL;
			newteamtype->Script = const_cast<ScriptTypeClass*>(ScriptTypeClass::As_Pointer("PARADROPINF_SCRIPT"));

			if (newteamtype->Script == nullptr) {
				ScriptTypeClass* newscripttype = new ScriptTypeClass();

				if (newscripttype == nullptr)
					return;

				strcpy(newscripttype->IniName, "PARADROPINF_SCRIPT");
				newscripttype->MissionCount = 2;
				newscripttype->MissionList[0].Mission = SMISSION_ATT_WAYPT;
				newscripttype->MissionList[0].Data.Value = WAYPT_SPECIAL;
				newscripttype->MissionList[1].Mission = SMISSION_DO;
				newscripttype->MissionList[1].Data.Mission = MISSION_RETREAT;

				newteamtype->Script = newscripttype;
			}

			newteamtype->TaskForce = const_cast<TaskForceClass*>(TaskForceClass::As_Pointer("PARADROPINF_TASKFORCE"));

			if (newteamtype->TaskForce == nullptr) {
				TaskForceClass* newtaskforce = new TaskForceClass();

				if (newtaskforce == nullptr)
					return;

				strcpy(newtaskforce->IniName, "PARADROPINF_TASKFORCE");
				newtaskforce->ClassCount = 2;
				newtaskforce->Members[0].Class = InfantryTypeClass::As_Pointer("E1");
				newtaskforce->Members[0].Quantity = AircraftTypeClass::As_Pointer("BADGER")->Max_Passengers();
				newtaskforce->Members[1].Class = AircraftTypeClass::As_Pointer("BADGER");
				newtaskforce->Members[1].Quantity = 1;

				newteamtype->TaskForce = newtaskforce;
			}

			newteamtype->House = this->House;
			ttype = newteamtype;
		}
	}

	if (ttype != NULL) {
		Scen->Waypoint[WAYPT_SPECIAL] = Map.Nearby_Location(*cell, SPEED_FOOT);
		if (Do_Reinforcements(ttype)) {

			// Mark the aircraft as a loaner so it is able to exit the map
			AircraftClass* spawnedAircraft = Aircrafts[Aircrafts.Count() - 1];
			spawnedAircraft->IsALoaner = true;
			AircraftClassExtension* aircraftext = Extension::Fetch<AircraftClassExtension>(spawnedAircraft);
			aircraftext->IsParadropReinforcement = true;
		}
	}

	//				Create_Air_Reinforcement(this, AIRCRAFT_BADGER, 1, MISSION_HUNT, ::As_Target(cell), TARGET_NONE, INFANTRY_E1);
	// if (this == PlayerPtr) {
	// 	Map.IsTargettingMode = SPC_NONE;
	// }
}


/**
 *  Main function for patching the hooks.
 */
void SuperClassExtension_Hooks()
{
    /**
     *  Initialises the extended class.
     */
    SuperClassExtension_Init();

	Patch_Jump(0x0060C880, &SuperClassFake::_Do_DropPods);
}
