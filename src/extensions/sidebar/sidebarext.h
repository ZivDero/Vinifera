/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *
 *  @project       Vinifera
 *
 *  @file          SCENARIOEXT.H
 *
 *  @author        CCHyper
 *
 *  @brief         Extended SidebarClass class.
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

#include "always.h"
#include "extension.h"
#include "sidebar.h"


class SidebarClassExtension final : public GlobalExtensionClass<SidebarClass>
{
public:
    enum SidebarTabType
    {
        SIDEBAR_TAB_STRUCTURE,
        SIDEBAR_TAB_INFANTRY,
        SIDEBAR_TAB_UNIT,
        SIDEBAR_TAB_SPECIAL,

        SIDEBAR_TAB_COUNT,
        SIDEBAR_TAB_NONE = -1
    };

    enum SidebarExtGeneralEnums
    {
        COLUMN_Y = 54,
        BUTTON_REPAIR_X_OFFSET = 36,
        UP_X_OFFSET = 5,				                            // Scroll up arrow coordinates.
        UP_Y_OFFSET = COLUMN_Y - 1,
        DOWN_X_OFFSET = UP_X_OFFSET,				                // Scroll down arrow coordinates.
        DOWN_Y_OFFSET = UP_Y_OFFSET,
        TAB_Y_OFFSET = 24,
        TAB_ONE_X_OFFSET = 20,
        TAB_TWO_X_OFFSET = TAB_ONE_X_OFFSET + 35,
        TAB_THREE_X_OFFSET = TAB_TWO_X_OFFSET + 35,
        TAB_FOUR_X_OFFSET = TAB_THREE_X_OFFSET + 35,
    };

    enum ButtonNumberType {
        BUTTON_TAB_1 = 115,
        BUTTON_TAB_2,
        BUTTON_TAB_3,
        BUTTON_TAB_4
    };

    class TabButtonClass : public ControlClass
    {
    private:
        enum
        {
            FLASH_RATE = 7
        };

    public:
        TabButtonClass();
        TabButtonClass(unsigned id, const ShapeFileStruct* shapes, int x, int y, ConvertClass* drawer = CameoDrawer, int w = 0, int h = 0);
        virtual ~TabButtonClass() override = default;

        virtual bool Action(unsigned flags, KeyNumType& key) override;
        virtual void Disable() override;
        virtual void Enable() override;
        virtual bool Draw_Me(bool forced = false) override;
        virtual void Set_Shape(const ShapeFileStruct* data, int width = 0, int height = 0);
        
        const ShapeFileStruct* Get_Shape_Data() const { return ShapeData; }
        void Start_Flashing();
        void Stop_Flashing();
        void Select();
        void Deselect();

    public:
        int DrawX;
        int DrawY;
        ConvertClass* ShapeDrawer;
        const ShapeFileStruct* ShapeData;

        bool IsFlashing;
        CDTimerClass<SystemTimerClass> FlashTimer;
        bool FlashState;

        bool IsSelected;
        bool IsDrawn;
    };

    class ViniferaSelectClass : public SidebarClass::StripClass::SelectClass
    {
    public:
        ViniferaSelectClass() = default;
        ViniferaSelectClass(const NoInitClass& x) : SelectClass(x) {}

        virtual void On_Mouse_Enter();
        virtual void On_Mouse_Leave();

        static void Check_Hover(GadgetClass* gadget, int mousex, int mousey);

    public:
        bool MousedOver = false;

        static ViniferaSelectClass* LastHovered;
    };

public:
        IFACEMETHOD(Load)(IStream *pStm);
        IFACEMETHOD(Save)(IStream *pStm, BOOL fClearDirty);

public:
        SidebarClassExtension(const SidebarClass *this_ptr);
        SidebarClassExtension(const NoInitClass &noinit);
        virtual ~SidebarClassExtension();

        virtual int Size_Of() const override;
        virtual void Detach(TARGET target, bool all = true) override;
        virtual void Compute_CRC(WWCRCEngine &crc) const override;

        virtual const char *Name() const override { return "Sidebar"; }
        virtual const char *Full_Name() const override { return "Sidebar"; }

        void Init_Strips();
        void Init_IO();
        void Init_For_House();
        void entry_84();
        bool Change_Tab(SidebarTabType index);

        SidebarClass::StripClass& Current_Tab() { return Column[TabIndex];}
        SidebarClass::StripClass& Get_Tab(RTTIType type) { return Column[Which_Tab(type)]; }
        SidebarTabType First_Active_Tab();

        static SidebarTabType Which_Tab(RTTIType type);

        static int Max_Visible(bool one_strip = false)
        {
            if (SidebarSurface && SidebarClass::SidebarShape)
            {
                int total = (SidebarRect.Height - SidebarClass::SidebarBottomShape->Get_Height() - SidebarClass::SidebarShape->Get_Height()) /
                    SidebarClass::SidebarMiddleShape->Get_Height();

                if (one_strip)
                    return total;
                
                return total * 2;
            }
            else
            {
                return SidebarClass::StripClass::MAX_VISIBLE;
            }
        }

    public:
        /**
         *  Index of the current sidebar tab.
         */
        SidebarTabType TabIndex;

        /**
         *  Replacement strips.
         */
        SidebarClass::StripClass Column[SIDEBAR_TAB_COUNT];

        /**
         *  Replacement select buttons.
         */
        ViniferaSelectClass SelectButton[SIDEBAR_TAB_COUNT][SidebarClass::StripClass::MAX_BUILDABLES];

        /**
        *  Buttons for the tabs.
        */
        TabButtonClass TabButtons[SIDEBAR_TAB_COUNT];

        RTTIType LastBuildingRTTI = RTTI_NONE;
        int LastBuildingHeapID = 0;
};