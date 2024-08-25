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
#include "drawshape.h"
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

    int max_visible = Max_Visible(true);

    for (int i = 0; i < SIDEBAR_TAB_COUNT; i++)
    {
        new (&Column[i]) SidebarClass::StripClass(NoInitClass());
        Column[i].X = SidebarClass::COLUMN_ONE_X;
        Column[i].Y = COLUMN_Y;
        Column[i].Size = Rect(SidebarClass::COLUMN_ONE_X, SidebarClass::COLUMN_ONE_Y, SidebarClass::StripClass::OBJECT_WIDTH, SidebarClass::StripClass::OBJECT_HEIGHT * max_visible);
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


void SidebarClassExtension::Init_Strips()
{
    int max_visible = Max_Visible(true);

    for (int i = 0; i < SIDEBAR_TAB_COUNT; i++)
    {
        new (&Column[i]) SidebarClass::StripClass(NoInitClass());
        Column[i].X = SidebarClass::COLUMN_ONE_X;
        Column[i].Y = COLUMN_Y;
        Column[i].Size = Rect(SidebarClass::COLUMN_ONE_X, SidebarClass::COLUMN_ONE_Y, SidebarClass::StripClass::OBJECT_WIDTH, SidebarClass::StripClass::OBJECT_HEIGHT * max_visible);
    }
}


void SidebarClassExtension::Init_IO()
{
    TabButtons[0].IsSticky = true;
    TabButtons[0].ID = BUTTON_TAB_1;
    TabButtons[0].Y = 148;
    TabButtons[0].DrawX = -480;
    TabButtons[0].DrawY = 3;
    TabButtons[0].IsPressed = false;
    TabButtons[0].IsToggleType = true;

    TabButtons[1].IsSticky = true;
    TabButtons[1].ID = BUTTON_TAB_2;
    TabButtons[1].Y = 148;
    TabButtons[1].DrawX = -480;
    TabButtons[1].DrawY = 3;
    TabButtons[1].IsPressed = false;
    TabButtons[1].IsToggleType = true;

    TabButtons[2].IsSticky = true;
    TabButtons[2].ID = BUTTON_TAB_3;
    TabButtons[2].Y = 148;
    TabButtons[2].DrawX = -480;
    TabButtons[2].DrawY = 3;
    TabButtons[2].IsPressed = false;
    TabButtons[2].IsToggleType = true;

    TabButtons[3].IsSticky = true;
    TabButtons[3].ID = BUTTON_TAB_4;
    TabButtons[3].Y = 148;
    TabButtons[3].DrawX = -480;
    TabButtons[3].DrawY = 3;
    TabButtons[3].IsPressed = false;
    TabButtons[3].IsToggleType = true;
}


void SidebarClassExtension::entry_84()
{
    TabButtons[0].Set_Position(SidebarRect.X + TAB_ONE_X_OFFSET, SidebarRect.Y + TAB_Y_OFFSET);
    TabButtons[0].Flag_To_Redraw();
    TabButtons[0].DrawX = -SidebarRect.X;

    TabButtons[1].Set_Position(SidebarRect.X + TAB_TWO_X_OFFSET, TabButtons[0].Y);
    TabButtons[1].Flag_To_Redraw();
    TabButtons[1].DrawX = -SidebarRect.X;

    TabButtons[2].Set_Position(SidebarRect.X + TAB_THREE_X_OFFSET, TabButtons[1].Y);
    TabButtons[2].Flag_To_Redraw();
    TabButtons[2].DrawX = -SidebarRect.X;

    TabButtons[3].Set_Position(SidebarRect.X + TAB_FOUR_X_OFFSET, TabButtons[2].Y);
    TabButtons[3].Flag_To_Redraw();
    TabButtons[3].DrawX = -SidebarRect.X;
}


void SidebarClassExtension::Init_For_House()
{
    TabButtons[0].Set_Shape(MFCC::RetrieveT<ShapeFileStruct>("TAB-BLD.SHP"));
    TabButtons[0].ShapeDrawer = SidebarDrawer;

    TabButtons[1].Set_Shape(MFCC::RetrieveT<ShapeFileStruct>("TAB-INF.SHP"));
    TabButtons[1].ShapeDrawer = SidebarDrawer;

    TabButtons[2].Set_Shape(MFCC::RetrieveT<ShapeFileStruct>("TAB-UNT.SHP"));
    TabButtons[2].ShapeDrawer = SidebarDrawer;

    TabButtons[3].Set_Shape(MFCC::RetrieveT<ShapeFileStruct>("TAB-SPC.SHP"));
    TabButtons[3].ShapeDrawer = SidebarDrawer;
}


void SidebarClassExtension::Change_Tab(SidebarTabType index)
{
    Column[TabIndex].Deactivate();
    TabIndex = index;
    Column[TabIndex].Activate();
    Map.IsToFullRedraw = true;
}

SidebarClassExtension::SidebarTabType SidebarClassExtension::Which_Tab(RTTIType type)
{
    switch (type)
    {
    case RTTI_BUILDINGTYPE:
    case RTTI_BUILDING:
        return SIDEBAR_TAB_STRUCTURE;

    case RTTI_INFANTRYTYPE:
    case RTTI_INFANTRY:
        return SIDEBAR_TAB_INFANTRY;

    case RTTI_UNITTYPE:
    case RTTI_UNIT:
        return SIDEBAR_TAB_UNIT;

    case RTTI_AIRCRAFTTYPE:
    case RTTI_AIRCRAFT:
    case RTTI_SUPERWEAPONTYPE:
    case RTTI_SUPERWEAPON:
    case RTTI_SPECIAL:
    default:
        return SIDEBAR_TAB_SPECIAL;
    }
}


SidebarClassExtension::TabButtonClass::TabButtonClass() :
ToggleClass(0, 0, 0, 0, 0),
DrawX(0),
DrawY(0),
ShapeDrawer(CameoDrawer),
ShapeData(nullptr),
State(TAB_STATE_NORMAL),
FlashTimer(0),
FlashState(false),
IsDrawn(false)
{
    IsToggleType = true;
}


SidebarClassExtension::TabButtonClass::TabButtonClass(unsigned id, const ShapeFileStruct* shapes, int x, int y, ConvertClass* drawer, int w, int h) :
ToggleClass(id, x, y, w, h),
DrawX(0),
DrawY(0),
ShapeDrawer(drawer),
ShapeData(shapes),
State(TAB_STATE_NORMAL),
FlashTimer(0),
FlashState(false),
IsDrawn(false)
{
    IsToggleType = true;
}


bool SidebarClassExtension::TabButtonClass::Draw_Me(bool forced)
{
    // IsDisabled - is the button unresponsive to the user's input?
    // IsPressed - is the user currently pressing the button?
    // IsOn - is the button currently in the "true" state?

    if (!ControlClass::Draw_Me(forced))
        return false;

    if (!ShapeData)
        return false;

    if (!ShapeDrawer)
        return false;


    int shapenum = 0;

    // A disabled tab always looks darkened
    if (IsDisabled)
    {
        shapenum = 2;
    }
    // Selected
    else if (IsOn)
    {
        shapenum = 1;
    }
    // Currently held down
    else if (IsPressed)
    {
        shapenum = 4;
    }
    else switch (State)
    {
    case TAB_STATE_NORMAL:
        // Just normal unselected tab
        shapenum = 0;
        break;

    case TAB_STATE_FLASHING:
        if (FlashTimer.Expired())
        {
            FlashState = !FlashState;
            FlashTimer = FLASH_RATE;
        }

        shapenum = FlashState ? 4 : 3;
        break;
    }

    CC_Draw_Shape(SidebarSurface, ShapeDrawer, ShapeData, shapenum, &Point2D(X + DrawX, Y + DrawY), &ScreenRect, SHAPE_NORMAL, 0, 0, ZGRAD_GROUND, 1000, nullptr, 0, 0);
    IsDrawn = true;
    return true;
}


void SidebarClassExtension::TabButtonClass::Set_Shape(const ShapeFileStruct* data, int width, int height)
{
    ShapeData = data;
    if (ShapeData)
    {
        Width = ShapeData->Get_Width();
        Height = ShapeData->Get_Height();
    }

    if (width != 0)
        Width = width;

    if (height != 0)
        Height = height;
}


void SidebarClassExtension::TabButtonClass::Set_State(TabButtonState state)
{
    State = state;
    switch (State)
    {
    case TAB_STATE_NORMAL:
        FlashTimer.Stop();
        FlashState = false;
        break;

    case TAB_STATE_FLASHING:
        FlashTimer.Start();
        FlashTimer = FLASH_RATE;
        FlashState = false;
    }
}

