/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *
 *  @project       Vinifera
 *
 *  @file          HOUSEEXT.H
 *
 *  @author        CCHyper
 *
 *  @brief         Extended HouseClass class.
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
#pragma once

#include "abstractext.h"
#include "asserthandler.h"
#include "house.h"
#include "housetype.h"


class DECLSPEC_UUID(UUID_HOUSE_EXTENSION)
HouseClassExtension final : public AbstractClassExtension
{
    public:
        /**
         *  IPersist
         */
        IFACEMETHOD(GetClassID)(CLSID *pClassID);

        /**
         *  IPersistStream
         */
        IFACEMETHOD(Load)(IStream *pStm);
        IFACEMETHOD(Save)(IStream *pStm, BOOL fClearDirty);

    public:
        HouseClassExtension(const HouseClass *this_ptr = nullptr);
        HouseClassExtension(const NoInitClass &noinit);
        virtual ~HouseClassExtension();

        virtual int Size_Of() const override;
        virtual void Detach(TARGET target, bool all = true) override;
        virtual void Compute_CRC(WWCRCEngine &crc) const override;

        virtual const char *Name() const override { return reinterpret_cast<const HouseClass *>(This())->Class->Name(); }
        virtual const char *Full_Name() const override { return reinterpret_cast<const HouseClass *>(This())->Class->Full_Name(); }
        
        virtual HouseClass *This() const override { return reinterpret_cast<HouseClass *>(AbstractClassExtension::This()); }
        virtual const HouseClass *This_Const() const override { return reinterpret_cast<const HouseClass *>(AbstractClassExtension::This_Const()); }
        virtual RTTIType What_Am_I() const override { return RTTI_HOUSE; }

    public:
        int StrengthenDestroyedCost;

        /**
         *  If we are currently expanding our base towards a resourceful location,
         *  this records the cell that we are expanding towards.
         */
        Cell NextExpansionPointLocation;

        /**
         *  Locations that we should never expand towards.
         *  Basically, locations that are unreachable.
         */
        Cell PermanentlyBlockedExpansionPointLocations[20];

        /**
         *  Records whether the AI has reached its expansion point.
         *  If yes, the AI should build a refinery.
         */
        bool ShouldBuildRefinery;

        /**
         *  Set when the AI has built its first barracks during the game.
         *  Used to figure out whether the AI should reset its TeamDelay
         *  timer when it has built a barracks.
         */
        bool HasBuiltFirstBarracks;

        /**
         *  Records when the AI last checked for excess refineries.
         */
        int LastExcessRefineryCheckFrame;

        /**
         *  Records when the AI last checked for sleeping harvesters.
         */
        int LastSleepingHarvesterCheckFrame;

        /**
         *  Defines whether the AI has already performed a final "desperate vehicle charge".
         *  If it has been done, then there is no need to do it again.
         */
        bool HasPerformedVehicleCharge;

        /**
         *  Records a value whether the current structure build choice 
         *  was made under threat of getting rushed early in the game.
         */
        bool IsUnderStartRushThreat;

        FactoryClass* ShipFactory;
        int ShipFactoryCount;

        template<typename T>
        class GlobalArgument
        {
        public:
            void Set(T value)
            {
                ASSERT_STACKDUMP_PRINT(_isset == false, "Tried to reset an argument that wasn't used!");
                _value = value;
                _isset = true;
            }

            T Get()
            {
                ASSERT_STACKDUMP_PRINT(_isset == true, "Tried to get an argument that wasn't supplied!");
                _isset = false;
                return _value;
            }

        private:
            T _value;
            bool _isset = false;
        };

        static GlobalArgument<int> Begin_Production_IsNaval;
        static GlobalArgument<int> Suspend_Production_IsNaval;
        static GlobalArgument<int> Abandon_Production_IsNaval;
        static GlobalArgument<int> Fetch_Factory_IsNaval;
        static GlobalArgument<int> Set_Factory_IsNaval;
        static GlobalArgument<int> Place_Object_IsNaval;
        static GlobalArgument<int> Factory_Count_IsNaval;
        static GlobalArgument<int> Factory_Counter_IsNaval;
};
