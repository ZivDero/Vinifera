/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *
 *  @project       Vinifera
 *
 *  @file          SidebarEXT.CPP
 *
 *  @author        ZivDero
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
#include "sidebarext.h"
#include "sidebarext.h"
#include "tibsun_globals.h"
#include "tibsun_defines.h"
#include "ccini.h"
#include "noinit.h"
#include "swizzle.h"
#include "vinifera_saveload.h"
#include "asserthandler.h"
#include "debughandler.h"
#include "language.h"
#include "tooltip.h"
#include "mouse.h"


SidebarClass::StripClass* Column[SidebarClassExtension::SIDEBAR_TAB_COUNT];
SidebarClass::StripClass::SelectClass SelectButton[SidebarClassExtension::SIDEBAR_TAB_COUNT][SidebarClass::StripClass::MAX_BUILDABLES];

/**
 *  Class constructor.
 *  
 *  @author: ZivDero
 */
SidebarClassExtension::SidebarClassExtension(const SidebarClass *this_ptr) :
    GlobalExtensionClass(this_ptr)
{
    //if (this_ptr) EXT_DEBUG_TRACE("SidebarClassExtension::SidebarClassExtension - 0x%08X\n", (uintptr_t)(ThisPtr));

    int button_count = Button_Count(true);

    for (int i = 0; i < SIDEBAR_TAB_COUNT; i++)
    {
        Column[i] = new SidebarClass::StripClass(NoInitClass());
        Column[i]->X = SidebarClass::COLUMN_ONE_X;
        Column[i]->Y = SidebarClass::COLUMN_ONE_Y;
        Column[i]->Size = Rect(SidebarClass::COLUMN_ONE_X, SidebarClass::COLUMN_ONE_Y, SidebarClass::StripClass::OBJECT_WIDTH, SidebarClass::StripClass::OBJECT_HEIGHT * button_count);
    }
}


/**
 *  Class no-init constructor.
 *  
 *  @author: ZivDero
 */
SidebarClassExtension::SidebarClassExtension(const NoInitClass &noinit) :
    GlobalExtensionClass(noinit)
{
    //EXT_DEBUG_TRACE("SidebarClassExtension::SidebarClassExtension(NoInitClass) - 0x%08X\n", (uintptr_t)(ThisPtr));
}


/**
 *  Class destructor.
 *  
 *  @author: ZivDero
 */
SidebarClassExtension::~SidebarClassExtension()
{
    //EXT_DEBUG_TRACE("SidebarClassExtension::~SidebarClassExtension - 0x%08X\n", (uintptr_t)(ThisPtr));

    for (int i = 0; i < SIDEBAR_TAB_COUNT; i++)
    {
        if (Column[i] != nullptr)
            delete Column[i];
    }
}


/**
 *  Initializes an object from the stream where it was saved previously.
 *  
 *  @author: ZivDero
 */
HRESULT SidebarClassExtension::Load(IStream *pStm)
{
    //EXT_DEBUG_TRACE("SidebarClassExtension::Load - 0x%08X\n", (uintptr_t)(This()));

    HRESULT hr = GlobalExtensionClass::Load(pStm);
    if (FAILED(hr)) {
        return E_FAIL;
    }

    new (this) SidebarClassExtension(NoInitClass());
    
    return hr;
}


/**
 *  Saves an object to the specified stream.
 *  
 *  @author: ZivDero
 */
HRESULT SidebarClassExtension::Save(IStream *pStm, BOOL fClearDirty)
{
    //EXT_DEBUG_TRACE("SidebarClassExtension::Save - 0x%08X\n", (uintptr_t)(This()));

    HRESULT hr = GlobalExtensionClass::Save(pStm, fClearDirty);
    if (FAILED(hr)) {
        return hr;
    }

    return hr;
}


/**
 *  Return the raw size of class data for save/load purposes.
 *  
 *  @author: ZivDero
 */
int SidebarClassExtension::Size_Of() const
{
    //EXT_DEBUG_TRACE("SidebarClassExtension::Size_Of - 0x%08X\n", (uintptr_t)(This()));

    return sizeof(*this);
}


/**
 *  Removes the specified target from any targeting and reference trackers.
 *  
 *  @author: ZivDero
 */
void SidebarClassExtension::Detach(TARGET target, bool all)
{
    //EXT_DEBUG_TRACE("SidebarClassExtension::Detach - 0x%08X\n", (uintptr_t)(This()));
}


/**
 *  Compute a unique crc value for this instance.
 *  
 *  @author: ZivDero
 */
void SidebarClassExtension::Compute_CRC(WWCRCEngine &crc) const
{
    //EXT_DEBUG_TRACE("SidebarClassExtension::Compute_CRC - 0x%08X\n", (uintptr_t)(This()));
}

/**
 *  Initialises any values for this instance.
 *
 *  @author: ZivDero
 */
void SidebarClassExtension::Init_Clear()
{
    //EXT_DEBUG_TRACE("ScenarioClassExtension::Init_Clear - 0x%08X\n", (uintptr_t)(This()));

    TabIndex = SIDEBAR_TAB_STRUCTURE;

    for (int i = 0; i < SIDEBAR_TAB_COUNT; i++)
        Column[i]->Init_Clear();
}

/**
 *  Initialises any values for this instance.
 *
 *  @author: ZivDero
 */
void SidebarClassExtension::Init_IO()
{
    //EXT_DEBUG_TRACE("ScenarioClassExtension::Init_Clear - 0x%08X\n", (uintptr_t)(This()));

    for (int i = 0; i < SIDEBAR_TAB_COUNT; i++)
        Column[i]->Init_IO(i);
}

/**
 *  Initialises any values for this instance.
 *
 *  @author: ZivDero
 */
void SidebarClassExtension::Activate()
{
    //EXT_DEBUG_TRACE("ScenarioClassExtension::Init_Clear - 0x%08X\n", (uintptr_t)(This()));

    Column[TabIndex]->Activate();
}


void SidebarClassExtension::Entry_84_Tooltips()
{
    SidebarClass::Repair.Set_Position(SidebarRect.X + 31, SidebarRect.Y - 9);
    SidebarClass::Repair.Flag_To_Redraw();
    SidebarClass::Repair.DrawX = -SidebarRect.X;

    SidebarClass::Sell.Set_Position(SidebarClass::Repair.X + 27, SidebarClass::Repair.Y);
    SidebarClass::Sell.Flag_To_Redraw();
    SidebarClass::Sell.DrawX = -SidebarRect.X;

    SidebarClass::Power.Set_Position(SidebarClass::Sell.X + 27, SidebarClass::Sell.Y);
    SidebarClass::Power.Flag_To_Redraw();
    SidebarClass::Power.DrawX = -SidebarRect.X;

    SidebarClass::Waypoint.Set_Position(SidebarClass::Power.X + 27, SidebarClass::Power.Y);
    SidebarClass::Waypoint.Flag_To_Redraw();
    SidebarClass::Waypoint.DrawX = -SidebarRect.X;

    if (ToolTipHandler)
    {
        ToolTip tooltip;

        for (int i = 0; i < 2; ++i)
        {
            for (int j = 0; j < 100; ++j)
            {
                ToolTipHandler->Remove((j | (i << 8)) + 1000);
            }
        }

        int button_count = Button_Count();

        SidebarClass::StripClass::UpButton[0].Set_Position(SidebarRect.X + SidebarClass::COLUMN_ONE_X + SidebarClass::StripClass::UP_X_OFFSET, SidebarClass::StripClass::OBJECT_HEIGHT * button_count + SidebarRect.Y + SidebarClass::StripClass::UP_Y_OFFSET);
        SidebarClass::StripClass::UpButton[0].Flag_To_Redraw();
        SidebarClass::StripClass::UpButton[0].DrawX = -SidebarRect.X;
        SidebarClass::StripClass::DownButton[0].Set_Position(SidebarRect.X + SidebarClass::COLUMN_TWO_X + SidebarClass::StripClass::DOWN_X_OFFSET, SidebarClass::StripClass::OBJECT_HEIGHT * button_count + SidebarRect.Y + SidebarClass::StripClass::DOWN_Y_OFFSET);
        SidebarClass::StripClass::DownButton[0].Flag_To_Redraw();
        SidebarClass::StripClass::DownButton[0].DrawX = -SidebarRect.X;

        for (int tab = 0; tab < SIDEBAR_TAB_COUNT; tab++)
        {
            for (int i = 0; i < button_count; i++)
            {
                const int x = SidebarRect.X + ((i % 2 == 0) ? SidebarClass::COLUMN_ONE_X : SidebarClass::COLUMN_TWO_X);
                const int y = SidebarRect.Y + SidebarClass::COLUMN_ONE_Y + ((i / 2) * SidebarClass::StripClass::OBJECT_HEIGHT);
                SidebarExtension->SelectButton[tab][i].Set_Position(x, y);
                tooltip.Region = Rect(SidebarExtension->SelectButton[tab][i].X, SidebarExtension->SelectButton[tab][i].Y, SidebarExtension->SelectButton[tab][i].Width, SidebarExtension->SelectButton[tab][i].Height);
                tooltip.ID = (i | (tab << 8)) + 1000;
                tooltip.Text = TXT_NONE;
                ToolTipHandler->Add(&tooltip);

            }
        }

        tooltip.Region = Rect(SidebarClass::Repair.X, SidebarClass::Repair.Y, SidebarClass::Repair.Width, SidebarClass::Repair.Height);
        tooltip.ID = 101;
        tooltip.Text = 101;
        ToolTipHandler->Remove(tooltip.ID);
        ToolTipHandler->Add(&tooltip);

        tooltip.Region = Rect(SidebarClass::Power.X, SidebarClass::Power.Y, SidebarClass::Power.Width, SidebarClass::Power.Height);
        tooltip.ID = 102;
        tooltip.Text = 105;
        ToolTipHandler->Remove(tooltip.ID);
        ToolTipHandler->Add(&tooltip);

        tooltip.Region = Rect(SidebarClass::Sell.X, SidebarClass::Sell.Y, SidebarClass::Sell.Width, SidebarClass::Sell.Height);
        tooltip.ID = 103;
        tooltip.Text = 103;
        ToolTipHandler->Remove(tooltip.ID);
        ToolTipHandler->Add(&tooltip);

        tooltip.Region = Rect(SidebarClass::Waypoint.X, SidebarClass::Waypoint.Y, SidebarClass::Waypoint.Width, SidebarClass::Waypoint.Height);
        tooltip.ID = 105;
        tooltip.Text = 135;
        ToolTipHandler->Remove(tooltip.ID);
        ToolTipHandler->Add(&tooltip);
    }
}

void SidebarClassExtension::Init_Strips()
{
    int button_count = Button_Count(true);

    for (int i = 0; i < SIDEBAR_TAB_COUNT; i++)
    {
        Column[i] = new SidebarClass::StripClass(NoInitClass());
        Column[i]->X = SidebarClass::COLUMN_ONE_X;
        Column[i]->Y = SidebarClass::COLUMN_ONE_Y;
        Column[i]->Size = Rect(SidebarClass::COLUMN_ONE_X, SidebarClass::COLUMN_ONE_Y, SidebarClass::StripClass::OBJECT_WIDTH, SidebarClass::StripClass::OBJECT_HEIGHT * button_count);
    }
}


void SidebarClassExtension::Change_Tab(SidebarTabType index)
{
    Map.Column[SidebarExtension->TabIndex].Deactivate();
    SidebarExtension->TabIndex = index;
    Map.Column[SidebarExtension->TabIndex].Activate();
    Map.IsToFullRedraw = true;
}

