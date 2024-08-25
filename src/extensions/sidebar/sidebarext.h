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

        SIDEBAR_TAB_COUNT
    };

    enum SidebarExtGeneralEnums
    {
        COLUMN_ONE_Y = 54,
        COLUMN_TWO_Y = COLUMN_ONE_Y,
        BUTTON_REPAIR_X_OFFSET = 36,
        UP_X_OFFSET = 5,				                            // Scroll up arrow coordinates.
        UP_Y_OFFSET = COLUMN_ONE_Y - 1,
        DOWN_X_OFFSET = UP_X_OFFSET,				                // Scroll down arrow coordinates.
        DOWN_Y_OFFSET = UP_Y_OFFSET,                                
    };

    class TabButtonClass : public ToggleClass
    {
        enum TabButtonState
        {
            TAB_STATE_NORMAL,
            TAB_STATE_FLASHING
        };

        enum
        {
            FLASH_RATE = 15
        };

    public:
        TabButtonClass();
        TabButtonClass(unsigned id, const ShapeFileStruct* shapes, int x, int y, ConvertClass* drawer = CameoDrawer, int w = 0, int h = 0);
        virtual ~TabButtonClass() override = default;

        virtual bool Draw_Me(bool forced = false) override;
        virtual void Set_Shape(const ShapeFileStruct* data, int width = 0, int height = 0);
        const ShapeFileStruct* Get_Shape_Data() const { return ShapeData; }
        void Set_State(TabButtonState state);

    public:
        int DrawX;
        int DrawY;
        ConvertClass* ShapeDrawer;
        const ShapeFileStruct* ShapeData;
        TabButtonState State;
        CDTimerClass<SystemTimerClass> FlashTimer;
        bool FlashState;
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
        void Change_Tab(SidebarTabType index);

        SidebarClass::StripClass& Active_Tab() const { return *Column[TabIndex];}
        SidebarClass::StripClass& Get_Tab(RTTIType type) const { return *Column[Which_Tab(type)]; }

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
        SidebarClass::StripClass* Column[SIDEBAR_TAB_COUNT];

        /**
         *  Replacement select buttons.
         */
        SidebarClass::StripClass::SelectClass SelectButton[SIDEBAR_TAB_COUNT][SidebarClass::StripClass::MAX_BUILDABLES];

        /**
        *  Buttons for the tabs.
        */
        TabButtonClass* TabButtons[SIDEBAR_TAB_COUNT];
};
