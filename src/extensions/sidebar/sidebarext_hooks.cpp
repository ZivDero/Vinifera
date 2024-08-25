/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *
 *  @project       Vinifera
 *
 *  @file          SIDEBAREXT_HOOKS.CPP
 *
 *  @author        CCHyper
 *
 *  @brief         Contains the hooks for the extended SidebarClass.
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
#include "sidebarext_hooks.h"
#include "tibsun_globals.h"
#include "sidebar.h"
#include "technotype.h"
#include "technotypeext.h"
#include "supertype.h"
#include "supertypeext.h"
#include "spritecollection.h"
#include "bsurface.h"
#include "drawshape.h"
#include "extension.h"
#include "fatal.h"
#include "asserthandler.h"
#include "convert.h"
#include "debughandler.h"
#include "factory.h"
#include "fetchres.h"
#include "mouse.h"

#include "hooker.h"
#include "hooker_macros.h"
#include "sidebar.h"
#include "session.h"
#include "tibsun_functions.h"
#include "tibsun_globals.h"
#include "house.h"
#include "language.h"
#include "playmovie.h"
#include "rules.h"
#include "sidebarext.h"
#include "super.h"
#include "techno.h"
#include "textprint.h"
#include "vinifera_globals.h"
#include "voc.h"
#include "vox.h"
#include "event.h"
#include "tooltip.h"


static const ObjectTypeClass *_SidebarClass_StripClass_obj = nullptr;
static const SuperWeaponTypeClass *_SidebarClass_StripClass_spc = nullptr;
static BSurface *_SidebarClass_StripClass_CustomImage = nullptr;


/**
 *  A fake class for implementing new member functions which allow
 *  access to the "this" pointer of the intended class.
 *
 *  @note: This must not contain a constructor or deconstructor!
 *  @note: All functions must be prefixed with "_" to prevent accidental virtualization.
 */
static class SidebarClassFake final : public SidebarClass
{
public:
	void _One_Time();
	void _Init_Clear();
	void _Init_IO();
	void _Init_For_House();
	void _Init_Strips();
	bool _Factory_Link(FactoryClass* factory, RTTIType type, int id);
	bool _Add(RTTIType type, int id);
	bool _Activate(int control);
	bool _Scroll(bool up, int column);
	bool _Scroll_Page(bool up, int column);
	void _Draw_It(bool complete);
	void _AI(KeyNumType& input, Point2D& xy);
	void _Recalc();
	bool _Abandon_Production(RTTIType type, FactoryClass* factory);
	void _entry_84();
	const char* _Help_Text(int gadget_id);
	int _Max_Visible();
};


/**
 *  A fake class for implementing new member functions which allow
 *  access to the "this" pointer of the intended class.
 *
 *  @note: This must not contain a constructor or deconstructor!
 *  @note: All functions must be prefixed with "_" to prevent accidental virtualization.
 */
static class StripClassFake final : public SidebarClass::StripClass
{
public:
	void _One_Time(int id);
	void _Init_IO(int id);
	void _Init_For_House(int id);
	void _Activate();
	void _Deactivate();
	bool _Scroll(bool up);
	bool _Scroll_Page(bool up);
	bool _AI(KeyNumType& input, Point2D& xy);
	const char* _Help_Text(int gadget_id);
    void _Draw_It(bool complete);
	bool _Factory_Link(FactoryClass* factory, RTTIType type, int id);
};


/**
 *  Patch for including the extended class members in the creation process.
 *
 *  @warning: Do not touch this unless you know what you are doing!
 *
 *  @author: ZivDero
 */
DECLARE_PATCH(_SidebarClass_Constructor_Patch)
{
	GET_REGISTER_STATIC(SidebarClass*, this_ptr, esi); // "this" pointer.

	/**
	 *  Create the extended class instance.
	 */
	SidebarExtension = Extension::Singleton::Make<SidebarClass, SidebarClassExtension>(this_ptr);

	/**
	 *  Stolen bytes here.
	 */
	_asm
	{
		mov eax, this_ptr
		pop edi
		pop esi
		pop ebp
		pop ebx
		add esp, 14h
		ret
	}
}


/**
 *  Patch for including the extended class members in the destruction process.
 *
 *  @warning: Do not touch this unless you know what you are doing!
 *
 *  @author: ZivDero
 */
DECLARE_PATCH(_SidebarClass_Destructor_Patch)
{
	//GET_REGISTER_STATIC(SidebarClass*, this_ptr, edi);

	/**
	 *  Remove the extended class instance.
	 */
	Extension::Singleton::Destroy<SidebarClass, SidebarClassExtension>(SidebarExtension);

	/**
	 *  Stolen bytes here.
	 */
    _asm
	{
		pop edi
		pop esi
		pop ebp
		pop ebx
		ret
	}
}


/**
 *  #issue-487
 * 
 *  Adds support for PCX/PNG cameo icons.
 * 
 *  The following two patches store the PCX/PNG image for the factory object or special.
 * 
 *  @author: CCHyper
 */
DECLARE_PATCH(_SidebarClass_StripClass_ObjectTypeClass_Custom_Cameo_Image_Patch)
{
    GET_REGISTER_STATIC(const ObjectTypeClass *, obj, ebp);
    static const TechnoTypeClassExtension *technotypeext;
    static const ShapeFileStruct *shapefile;

    shapefile = obj->Get_Cameo_Data();

    _SidebarClass_StripClass_obj = obj;
    _SidebarClass_StripClass_CustomImage = nullptr;

    technotypeext = Extension::Fetch<TechnoTypeClassExtension>(reinterpret_cast<const TechnoTypeClass *>(obj));
    if (technotypeext->CameoImageSurface)
	{
        _SidebarClass_StripClass_CustomImage = technotypeext->CameoImageSurface;
    }

    _asm { mov eax, shapefile }

    JMP_REG(ebx, 0x005F5193);
}

DECLARE_PATCH(_SidebarClass_StripClass_SuperWeaponType_Custom_Cameo_Image_Patch)
{
    GET_REGISTER_STATIC(const SuperWeaponTypeClass *, supertype, eax);
    static const SuperWeaponTypeClassExtension *supertypeext;
    static const ShapeFileStruct *shapefile;

    shapefile = supertype->SidebarIcon;

    _SidebarClass_StripClass_spc = supertype;
    _SidebarClass_StripClass_CustomImage = nullptr;

    supertypeext = Extension::Fetch<SuperWeaponTypeClassExtension>(supertype);
    if (supertypeext->CameoImageSurface)
	{
        _SidebarClass_StripClass_CustomImage = supertypeext->CameoImageSurface;
    }

    _asm { mov ebx, shapefile }

    JMP(0x005F5220);
}


/**
 *  #issue-487
 * 
 *  Adds support for PCX/PNG cameo icons.
 * 
 *  @author: CCHyper
 */
static Point2D pointxy;
static Rect pcxrect;
DECLARE_PATCH(_SidebarClass_StripClass_Custom_Cameo_Image_Patch)
{
    GET_STACK_STATIC(SidebarClass::StripClass *, this_ptr, esp, 0x24);
    LEA_STACK_STATIC(Rect *, window_rect, esp, 0x34);
    GET_REGISTER_STATIC(int, pos_x, edi);
    GET_REGISTER_STATIC(int, pos_y, esi);
    GET_REGISTER_STATIC(const ShapeFileStruct *, shapefile, ebx);
    static BSurface *image_surface;

    image_surface = nullptr;

    /**
     *  Was a factory object or special image found?
     */
    if (_SidebarClass_StripClass_CustomImage) {
        image_surface = _SidebarClass_StripClass_CustomImage;
    }

    /**
     *  Draw the cameo pcx image.
     */
    if (image_surface) {
        pcxrect.X = window_rect->X + pos_x;
        pcxrect.Y = window_rect->Y + pos_y;
        pcxrect.Width = image_surface->Get_Width();
        pcxrect.Height = image_surface->Get_Height();

        SpriteCollection.Draw(pcxrect, *SidebarSurface, *image_surface);

    /**
     *  Draw shape cameo image.
     */
    } else if (shapefile) {
        pointxy.X = pos_x;
        pointxy.Y = pos_y;

        CC_Draw_Shape(SidebarSurface, CameoDrawer, shapefile, 0, &pointxy, window_rect, SHAPE_WIN_REL|SHAPE_NORMAL);
    }

    _SidebarClass_StripClass_CustomImage = nullptr;

    /**
     *  Next, draw the clock darken shape.
     */
draw_darken_shape:
    JMP(0x005F52F3);
}


void StripClassFake::_Draw_It(bool complete)
{
	if (IsToRedraw || complete)
	{
		IsToRedraw = false;
		RedrawSidebar = true;

		Rect rect = Rect(0, SidebarRect.Y, SidebarRect.Width, SidebarRect.Height);

		/*
		**	Redraw the scroll buttons.
		*/
		UpButton[0].Draw_Me(true);
		DownButton[0].Draw_Me(true);

		int maxvisible = SidebarClassExtension::Max_Visible();

		/*
		**	Loop through all the buildable objects that are visible in the strip and render
		**	them. Their Y offset may be adjusted if the strip is in the process of scrolling.
		*/
		for (int i = 0; i < maxvisible + (IsScrolling ? 1 : 0); i++)
		{
			bool production = false;
			bool completed = false;
			int  stage = 0;
			bool darken = false;
			ShapeFileStruct const* shapefile = nullptr;
			FactoryClass* factory = nullptr;
			int index = i + TopIndex;
			int x = i % 2 == 0 ? SidebarClass::COLUMN_ONE_X : SidebarClass::COLUMN_TWO_X;
			int y = SidebarClassExtension::COLUMN_ONE_Y + ((i / 2) * OBJECT_HEIGHT);

			bool isready = false;
			const char* state = nullptr;
			const char* name = nullptr;
			TechnoTypeClass const* obj = nullptr;

			/*
			**	If the strip is scrolling, then the offset is adjusted accordingly.
			*/
			if (IsScrolling)
			{
				y -= OBJECT_HEIGHT - Slid;
			}

			/*
			**	Fetch the shape number for the object type located at this current working
			**	slot. This shape pointer is used to draw the underlying graphic there.
			*/
			if (index < BuildableCount)
			{
				SpecialWeaponType spc = SPECIAL_NONE;

				if (Buildables[index].BuildableType != RTTI_SPECIAL)
				{
					obj = Fetch_Techno_Type(Buildables[index].BuildableType, Buildables[index].BuildableID);
					if (obj != nullptr)
					{
						name = obj->FullName;
						darken = false;

						/*
						**	If there is already a factory producing this kind of object, then all
						**	objects of this type are displays in a disabled state.
						*/
						if (obj->Kind_Of() == RTTI_BUILDINGTYPE)
						{
							darken = PlayerPtr->Fetch_Factory(Buildables[index].BuildableType) != nullptr;
						}

						/*
						**	If there is no factory that can produce this, or the factory that
						*	can produce this is currently busy,
						**	objects of this type are displays in a disabled state.
						*/
						if (!obj->Who_Can_Build_Me(true, true, true, PlayerPtr)
							|| !darken && PlayerPtr->Can_Build(Fetch_Techno_Type(Buildables[index].BuildableType, Buildables[index].BuildableID), false, false) == -1)
						{
							darken = true;
						}

						shapefile = obj->Get_Cameo_Data();

						factory = Buildables[index].Factory;
						if (factory != nullptr)
						{
							production = true;
							completed = factory->Has_Completed();
							if (completed)
							{
								/*
					            **	Display text showing that the object is ready to place.
					            */
								state = Fetch_String(TXT_READY);
							}
							stage = factory->Completion();
							darken = false;
						}
						else
						{
							production = false;
						}
					}
					else
					{
						shapefile = LogoShape;
					}

				}
				else
				{
					spc = (SpecialWeaponType)Buildables[index].BuildableID;

					name = SuperWeaponTypes[spc]->FullName;
					shapefile = Get_Special_Cameo(spc);

					production = true;
					completed = !PlayerPtr->SuperWeapon[spc]->Needs_Redraw();
					isready = PlayerPtr->SuperWeapon[spc]->Is_Ready();
					state = PlayerPtr->SuperWeapon[spc]->Ready_String();
					stage = PlayerPtr->SuperWeapon[spc]->Anim_Stage();
					darken = false;

					if (spc == SPECIAL_NONE)
					{
						shapefile = LogoShape;
					}
				}
			}
			else
			{
				shapefile = LogoShape;
				production = false;
			}

			/*
			**	Now that the shape of the object at the current working slot has been found,
			**	draw it and any graphic overlays as necessary.
			*/
			if (shapefile != LogoShape)
			{
				if (shapefile != nullptr)
				{
					Point2D drawpoint(x, y);
					CC_Draw_Shape(SidebarSurface, CameoDrawer, shapefile,
						0, &drawpoint, &rect, SHAPE_WIN_REL, 0, 0, ZGRAD_GROUND, 1000, nullptr, 0, 0, 0);
				}

				/*
				**	Darken this object because it cannot be produced or is otherwise
				**	unavailable.
				*/
				if (darken)
				{
					Point2D drawpoint(x, y);
					CC_Draw_Shape(SidebarSurface, SidebarDrawer, DarkenShape,
						0, &drawpoint, &rect, SHAPE_WIN_REL | SHAPE_DARKEN, 0, 0, ZGRAD_GROUND, 1000, nullptr, 0, 0, 0);
				}
			}

			if (name != nullptr)
			{
				Point2D drawpoint(x, y + OBJECT_NAME_OFFSET);
				Print_Cameo_Text(name, drawpoint, rect, OBJECT_WIDTH);
			}

			/*
			**	Draw the number of queued objects
			*/
			bool hasqueuecount = false;
			if (obj != nullptr)
			{
				RTTIType rtti = obj->Kind_Of();
				FactoryClass* factory = PlayerPtr->Fetch_Factory(rtti);

				if (factory != nullptr)
				{
					int total = factory->Total_Queued(*obj);
					if (total > 1 ||
						total > 0 && (factory->Object == nullptr ||
						factory->Object->Techno_Type_Class() != nullptr && factory->Object->Techno_Type_Class() != obj))
					{
						Point2D drawpoint(x + QUEUE_COUNT_X_OFFSET, y + TEXT_Y_OFFSET);
						Fancy_Text_Print("%d", SidebarSurface, &rect, &drawpoint, ColorScheme::As_Pointer("LightGrey", 1), COLOR_TBLACK, TPF_RIGHT | TPF_FULLSHADOW | TPF_8POINT, total);
						hasqueuecount = true;
					}
				}
			}

			/*
			**	Draw the overlapping clock shape if this is object is being constructed.
			**	If the object is completed, then display "Ready" with no clock shape.
			*/
			if (production)
			{
				if (state != nullptr)
				{
					Point2D drawpoint(x + TEXT_X_OFFSET, y + TEXT_Y_OFFSET);
					Fancy_Text_Print(state, SidebarSurface, &rect, &drawpoint, ColorScheme::As_Pointer("LightBlue", 1), COLOR_TBLACK, TPF_CENTER | TPF_FULLSHADOW | TPF_8POINT);
				}

				if (!completed)
				{
					int shapenum;
					const ShapeFileStruct* shape;
					Point2D drawpoint;

					if (isready)
					{
						shapenum = stage + 1;
						drawpoint = Point2D(x, y);
						shape = RechargeClockShape;
					}
					else
					{
						shapenum = stage + 1;
						drawpoint = Point2D(x, y);
						shape = ClockShape;
					}

					CC_Draw_Shape(SidebarSurface, SidebarDrawer, shape,
						shapenum, &drawpoint, &rect, SHAPE_WIN_REL | SHAPE_TRANS50, 0, 0, ZGRAD_GROUND, 1000, nullptr, 0, 0, 0);

					/*
					**	Display text showing that the construction is temporarily on hold.
					*/
					if (factory && (!factory->Is_Building() || factory->IsSuspended))
					{
						if (hasqueuecount)
						{
							Point2D drawpoint2(x, y + TEXT_Y_OFFSET);
							Fancy_Text_Print(TXT_HOLD, SidebarSurface, &rect, &drawpoint2, ColorScheme::As_Pointer("LightGrey", 1), COLOR_TBLACK, TPF_FULLSHADOW | TPF_8POINT);
						}
						else
						{
							Point2D drawpoint2(x + TEXT_X_OFFSET, y + TEXT_Y_OFFSET);
							Fancy_Text_Print(TXT_HOLD, SidebarSurface, &rect, &drawpoint2, ColorScheme::As_Pointer("LightGrey", 1), COLOR_TBLACK, TPF_CENTER | TPF_FULLSHADOW | TPF_8POINT);
						}
					}
				}
			}

		}

		LastSlid = Slid;
		return;
	}

	if (UpButton[0].IsDrawn)
	{
		RedrawSidebar = true;
		UpButton[0].IsDrawn = false;
	}

	if (DownButton[0].IsDrawn)
	{
		RedrawSidebar = true;
		DownButton[0].IsDrawn = false;
	}
}


bool SidebarClassFake::_Scroll(bool up, int column)
{
	if (*reinterpret_cast<int*>(0x007E492C))
		return false;

	bool scr = SidebarExtension->Active_Tab().Scroll(up);

	if (scr)
	{
		IsToRedraw = true;
		Flag_To_Redraw(false);
		return true;
	}

    Sound_Effect(Rule->ScoldSound);
	return false;
}


bool SidebarClassFake::_Scroll_Page(bool up, int column)
{
	bool scr = SidebarExtension->Active_Tab().Scroll_Page(up);

	if (scr)
	{
		IsToRedraw = true;
		Flag_To_Redraw(false);
		return true;
	}

    Sound_Effect(Rule->ScoldSound);
	return false;
}


void SidebarClassFake::_AI(KeyNumType& input, Point2D& xy)
{
	if (!Debug_Map) 
	{
		Activate(1);
	    Point2D newpoint(xy.X - 480, xy.Y);
	    SidebarExtension->Active_Tab().AI(input, newpoint);
	}

	if (IsSidebarActive)
	{

		/*
		**	If there are any buildings in the player's inventory, then allow the repair
		**	option.
		*/

		if (PlayerPtr->CurBuildings > 0)
		{
			Activate_Repair(true);
		}
		else
		{
			Activate_Repair(false);
		}

		if (input == (BUTTON_REPAIR | KN_BUTTON))
		{
			Repair_Mode_Control(-1);
		}

		if (input == (BUTTON_POWER | KN_BUTTON))
		{
			Power_Mode_Control(-1);
		}

		if (input == (BUTTON_WAYPOINT | KN_BUTTON))
		{
			Waypoint_Mode_Control(-1, false);
		}

		if (input == (BUTTON_SELL | KN_BUTTON))
		{
			Sell_Mode_Control(-1);
		}
	}

	if (!IsRepairMode && Repair.IsOn)
	{
		Repair.Turn_Off();
	}

	if (!IsSellMode && Sell.IsOn)
	{
		Sell.Turn_Off();
	}

	if (!IsPowerMode && Power.IsOn)
	{
		Power.Turn_Off();
	}

	if (!IsWaypointMode && Waypoint.IsOn)
	{
		Waypoint.Turn_Off();
	}

	PowerClass::AI(input, xy);
}


void SidebarClassFake::_Recalc()
{
    if (SidebarExtension->Active_Tab().Recalc())
    {
        IsToRedraw = true;
		Flag_To_Redraw();
    }
}


bool SidebarClassFake::_Abandon_Production(RTTIType type, FactoryClass* factory)
{
    return SidebarExtension->Get_Tab(type).Abandon_Production(factory);
}


const char* SidebarClassFake::_Help_Text(int gadget_id)
{
	const char* text = PowerClass::Help_Text(gadget_id);
	if (text == nullptr)
	{
	    return SidebarExtension->Column[SidebarExtension->TabIndex]->Help_Text(gadget_id - 1000);
	}
	return text;
}


int SidebarClassFake::_Max_Visible()
{
	return SidebarClassExtension::Max_Visible(true);
}


void SidebarClassFake::_Init_IO()
{
	PowerClass::Init_IO();

	SidebarRect.X = TacticalRect.Width + TacticalRect.X;
	SidebarRect.Y = 148;
	SidebarRect.Width = 641 - (TacticalRect.Width + TacticalRect.X);
	SidebarRect.Height = TacticalRect.Height + TacticalRect.Y - SidebarRect.Y;

	/*
	** Add the sidebar's buttons only if we're not in editor mode.
	*/
	if (!Debug_Map) {

		Repair.X = TacticalRect.Width + TacticalRect.X;
		Sell.X = TacticalRect.Width + TacticalRect.X + 27;
		Power.X = TacticalRect.Width + TacticalRect.X + 54;
		Waypoint.X = TacticalRect.Width + TacticalRect.X + 81;

		Repair.IsSticky = true;
		Repair.ID = BUTTON_REPAIR;
		Repair.Y = 148;
		Repair.DrawX = -480;
		Repair.DrawY = 3;
		Repair.field_3C = true;
		Repair.ShapeDrawer = SidebarDrawer;
		Repair.IsPressed = false;
		Repair.IsToggleType = true;
		Repair.ReflectButtonState = true;

		Sell.IsSticky = true;
		Sell.ID = BUTTON_SELL;
		Sell.Y = 148;
		Sell.DrawX = -480;
		Sell.DrawY = 3;
		Sell.field_3C = true;
		Sell.ShapeDrawer = SidebarDrawer;
		Sell.IsPressed = false;
		Sell.IsToggleType = true;
		Sell.ReflectButtonState = true;

		Power.IsSticky = true;
		Power.ID = BUTTON_POWER;
		Power.Y = 148;
		Power.DrawX = -480;
		Power.DrawY = 3;
		Power.field_3C = true;
		Power.ShapeDrawer = SidebarDrawer;
		Power.IsPressed = false;
		Power.IsToggleType = true;
		Power.ReflectButtonState = true;

		Waypoint.IsSticky = true;
		Waypoint.ID = BUTTON_WAYPOINT;
		Waypoint.Y = 148;
		Waypoint.DrawX = -480;
		Waypoint.DrawY = 3;
		Waypoint.field_3C = true;
		Waypoint.ShapeDrawer = SidebarDrawer;
		Waypoint.IsPressed = false;
		Waypoint.IsToggleType = true;
		Waypoint.ReflectButtonState = true;
		Waypoint.Enable();

		for (int i = 0; i < SidebarClassExtension::SIDEBAR_TAB_COUNT; i++)
			SidebarExtension->Column[i]->Init_IO(i);

		entry_84();

		/*
		** If a game was loaded & the sidebar was enabled, pop it up now.
		*/
		if (IsSidebarActive)
		{
			IsSidebarActive = false;
			Activate(1);
		}
	}
}


void SidebarClassFake::_Init_For_House()
{
	PowerClass::Init_For_House();

	PaletteClass pal("SIDEBAR.PAL");

	delete SidebarDrawer;
	SidebarDrawer = new ConvertClass(&pal, &pal, PrimarySurface, 1);

	Sell.Set_Shape(MFCC::RetrieveT<ShapeFileStruct>("SELL.SHP"), 0, 0);
	Sell.ShapeDrawer = SidebarDrawer;

	Power.Set_Shape(MFCC::RetrieveT<ShapeFileStruct>("POWER.SHP"), 0, 0);
	Power.ShapeDrawer = SidebarDrawer;

	Waypoint.Set_Shape(MFCC::RetrieveT<ShapeFileStruct>("WAYP.SHP"), 0, 0);
	Waypoint.ShapeDrawer = SidebarDrawer;

	Repair.Set_Shape(MFCC::RetrieveT<ShapeFileStruct>("REPAIR.SHP"), 0, 0);
	Repair.ShapeDrawer = SidebarDrawer;

	SidebarShape = MFCC::RetrieveT<ShapeFileStruct>("SIDE1.SHP");
	SidebarMiddleShape = MFCC::RetrieveT<ShapeFileStruct>("SIDE2.SHP");
	SidebarBottomShape = MFCC::RetrieveT<ShapeFileStruct>("SIDE3.SHP");
	SidebarAddonShape = MFCC::RetrieveT<ShapeFileStruct>("ADDON.SHP");

	for (int i = 0; i < SidebarClassExtension::SIDEBAR_TAB_COUNT; ++i)
		static_cast<StripClassFake*>(SidebarExtension->Column[i])->_Init_For_House(i);
}


void SidebarClassFake::_One_Time()
{
	PowerClass::One_Time();

	for (int i = 0; i < SidebarClassExtension::SIDEBAR_TAB_COUNT; i++)
		SidebarExtension->Column[i]->One_Time(i);

	/*
	**  Load the sidebar shapes in at this time.
	*/
    StripClass::RechargeClockShape = MFCC::RetrieveT<ShapeFileStruct>("RCLOCK2.SHP");
    StripClass::ClockShape = MFCC::RetrieveT<ShapeFileStruct>("GCLOCK2.SHP");
}


void SidebarClassFake::_Init_Clear()
{
	PowerClass::Init_Clear();

	IsToRedraw = true;
	IsRepairActive = false;
	IsUpgradeActive = false;
	IsUpgradeActive = false;

	SidebarExtension->TabIndex = SidebarClassExtension::SIDEBAR_TAB_STRUCTURE;

	for (int i = 0; i < SidebarClassExtension::SIDEBAR_TAB_COUNT; i++)
		SidebarExtension->Column[i]->Init_Clear();

	Activate(0);
}


void SidebarClassFake::_entry_84()
{
    SidebarRect.X = Options.SidebarOn ? TacticalRect.X + TacticalRect.Width : 0;
	SidebarRect.Y = 148;
	SidebarRect.Width = 168;
	SidebarRect.Height = TacticalRect.Y + TacticalRect.Height - 148;

	PowerClass::entry_84();

	if (!SidebarShape)
	{
		SidebarShape = MFCC::RetrieveT<ShapeFileStruct>("SIDEGDI1.SHP");
		SidebarMiddleShape = MFCC::RetrieveT<ShapeFileStruct>("SIDEGDI2.SHP");
		SidebarBottomShape = MFCC::RetrieveT<ShapeFileStruct>("SIDEGDI3.SHP");
	}

	Background.Set_Position(SidebarRect.X + 16, TacticalRect.Y);
	Background.Flag_To_Redraw();

	Repair.Set_Position(SidebarRect.X + SidebarClassExtension::BUTTON_REPAIR_X_OFFSET, SidebarRect.Y  + BUTTON_REPAIR_Y_OFFSET);
	Repair.Flag_To_Redraw();
	Repair.DrawX = -SidebarRect.X;

	Sell.Set_Position(Repair.X + BUTTON_SELL_X_OFFSET, Repair.Y);
	Sell.Flag_To_Redraw();
	Sell.DrawX = -SidebarRect.X;

	Power.Set_Position(Sell.X + BUTTON_POWER_X_OFFSET, Sell.Y);
	Power.Flag_To_Redraw();
	Power.DrawX = -SidebarRect.X;

	Waypoint.Set_Position(Power.X + BUTTON_WAYPOINT_X_OFFSET, Power.Y);
	Waypoint.Flag_To_Redraw();
	Waypoint.DrawX = -SidebarRect.X;

	if (ToolTipHandler)
	{
		ToolTip tooltip;

		for (int i = 0; i < 100; i++)
		{
			ToolTipHandler->Remove(1000 + i);
		}

		int max_visible = SidebarClassExtension::Max_Visible();

		StripClass::UpButton[0].Set_Position(SidebarRect.X + COLUMN_ONE_X + SidebarClassExtension::UP_X_OFFSET, SidebarRect.Y + StripClass::OBJECT_HEIGHT * max_visible / 2 + SidebarClassExtension::UP_Y_OFFSET);
		StripClass::UpButton[0].Flag_To_Redraw();
		StripClass::UpButton[0].DrawX = -SidebarRect.X;
		StripClass::DownButton[0].Set_Position(SidebarRect.X + COLUMN_TWO_X + SidebarClassExtension::DOWN_X_OFFSET, SidebarRect.Y + StripClass::OBJECT_HEIGHT * max_visible / 2 + SidebarClassExtension::DOWN_Y_OFFSET);
		StripClass::DownButton[0].Flag_To_Redraw();
		StripClass::DownButton[0].DrawX = -SidebarRect.X;

		for (int tab = 0; tab < SidebarClassExtension::SIDEBAR_TAB_COUNT; tab++)
		{
			for (int i = 0; i < max_visible; i++)
			{
				const int x = SidebarRect.X + ((i % 2 == 0) ? COLUMN_ONE_X : COLUMN_TWO_X);
				const int y = SidebarRect.Y + SidebarClassExtension::COLUMN_ONE_Y + ((i / 2) * StripClass::OBJECT_HEIGHT);
				SidebarExtension->SelectButton[tab][i].Set_Position(x, y);
			}
		}

		for (int i = 0; i < max_visible; i++)
		{
			tooltip.Region = Rect(SidebarExtension->SelectButton[0][i].X, SidebarExtension->SelectButton[0][i].Y, SidebarExtension->SelectButton[0][i].Width, SidebarExtension->SelectButton[0][i].Height);
			tooltip.ID = 1000 + i;
			tooltip.Text = TXT_NONE;
			ToolTipHandler->Add(&tooltip);
		}

		tooltip.Region = Rect(Repair.X, Repair.Y, Repair.Width, Repair.Height);
		tooltip.ID = BUTTON_REPAIR;
		tooltip.Text = TXT_REPAIR_MODE;
		ToolTipHandler->Remove(tooltip.ID);
		ToolTipHandler->Add(&tooltip);

		tooltip.Region = Rect(Power.X, Power.Y, Power.Width, Power.Height);
		tooltip.ID = BUTTON_POWER;
		tooltip.Text = TXT_POWER_MODE;
		ToolTipHandler->Remove(tooltip.ID);
		ToolTipHandler->Add(&tooltip);

		tooltip.Region = Rect(Sell.X, Sell.Y, Sell.Width, Sell.Height);
		tooltip.ID = BUTTON_SELL;
		tooltip.Text = TXT_SELL_MODE;
		ToolTipHandler->Remove(tooltip.ID);
		ToolTipHandler->Add(&tooltip);

		tooltip.Region = Rect(Waypoint.X, Waypoint.Y, Waypoint.Width, Waypoint.Height);
		tooltip.ID = BUTTON_WAYPOINT;
		tooltip.Text = TXT_WAYPOINTMODE;
		ToolTipHandler->Remove(tooltip.ID);
		ToolTipHandler->Add(&tooltip);
	}

	Background.Set_Position(Options.SidebarOn ? TacticalRect.X + TacticalRect.Width : 0, RadarButton.Height + RadarButton.Y);
	Background.Set_Size(SidebarSurface->Get_Width(), SidebarSurface->Get_Height() - RadarButton.Height + RadarButton.Y);
}


bool SidebarClassFake::_Activate(int control)
{
	bool old = IsSidebarActive;

	if (Session.Play && !Session.Singleplayer_Game())
		return old;

	/*
	**	Determine the new state of the sidebar.
	*/
	switch (control)
    {
	case -1:
		IsSidebarActive = IsSidebarActive == false;
		break;

	case 1:
		IsSidebarActive = true;
		break;

	default:
	case 0:
		IsSidebarActive = false;
		break;
	}

	/*
	**	Only if there is a change in the state of the sidebar will anything
	**	be done to change it.
	*/
	if (IsSidebarActive != old) {

		/*
		**	If the sidebar is activated but was on the right side of the screen, then
		**	activate it on the left side of the screen.
		*/
		if (IsSidebarActive)
		{
			entry_84();
			IsToRedraw = true;
			Repair.Zap();
			Add_A_Button(Repair);
			Sell.Zap();
			Add_A_Button(Sell);
			Power.Zap();
			Add_A_Button(Power);
			Waypoint.Zap();
			Add_A_Button(Waypoint);
			SidebarExtension->Active_Tab().Activate();
			Background.Zap();
			Add_A_Button(Background);
			RadarButton.Zap();
			Add_A_Button(RadarButton);
		}
		else
		{
			End_Ingame_Movie();
			Remove_A_Button(Repair);
			Remove_A_Button(Sell);
			Remove_A_Button(Power);
			Remove_A_Button(Waypoint);
			Remove_A_Button(Background);
			for (int i = 0; i < SidebarClassExtension::SIDEBAR_TAB_COUNT; i++)
				SidebarExtension->Column[i]->Deactivate();
			Remove_A_Button(RadarButton);
		}

		/*
		**	Since the sidebar status has changed, update the map so that the graphics
		**	will be rendered correctly.
		*/
		Flag_To_Redraw(2);
	}

	return old;
}


void SidebarClassFake::_Init_Strips()
{
	SidebarExtension->Init_Strips();
}


bool SidebarClassFake::_Factory_Link(FactoryClass* factory, RTTIType type, int id)
{
	return SidebarExtension->Get_Tab(type).Factory_Link(factory, type, id);
}


bool SidebarClassFake::_Add(RTTIType type, int id)
{
	if (!Debug_Map)
	{
		SidebarClassExtension::SidebarTabType column = SidebarClassExtension::Which_Tab(type);

		if (SidebarExtension->Column[column]->Add(type, id))
		{
			Activate(1);
			IsToRedraw = true;
			Flag_To_Redraw(false);
			return true;
		}
		return false;
	}

	return false;
}


void SidebarClassFake::_Draw_It(bool complete)
{
	complete |= IsToFullRedraw;
	Map.field_1214 = Rect();
	PowerClass::Draw_It(complete);

	DSurface* oldsurface = TempSurface;
	TempSurface = SidebarSurface;

	Rect rect(0, 0, SidebarSurface->Get_Width(), SidebarSurface->Get_Height());

	if (IsSidebarActive && (IsToRedraw || complete) && !Debug_Map)
	{
		if (complete || SidebarExtension->Active_Tab().IsToRedraw)
        {
			Point2D xy(0, SidebarRect.Y);
			CC_Draw_Shape(SidebarSurface, SidebarDrawer, SidebarShape, 0, &xy, &rect, SHAPE_WIN_REL, 0, 0, ZGRAD_GROUND, 1000, nullptr, 0, 0, 0);

			int max_visible = SidebarClassExtension::Max_Visible(true);
			int y = SidebarRect.Y + SidebarShape->Get_Height();

			for (int i = 0; i < max_visible; i++, y += SidebarMiddleShape->Get_Height())
			{
				xy = Point2D(0, y);
				CC_Draw_Shape(SidebarSurface, SidebarDrawer, SidebarMiddleShape, 0, &xy, &rect, SHAPE_WIN_REL, 0, 0, ZGRAD_GROUND, 1000, nullptr, 0, 0, 0);
			}

			xy = Point2D(0, y);
			CC_Draw_Shape(SidebarSurface, SidebarDrawer, SidebarBottomShape, 0, &xy, &rect, SHAPE_WIN_REL, 0, 0, ZGRAD_GROUND, 1000, nullptr, 0, 0, 0);

			xy = Point2D(0, y + SidebarBottomShape->Get_Height());
			CC_Draw_Shape(SidebarSurface, SidebarDrawer, SidebarAddonShape, 0, &xy, &rect, SHAPE_WIN_REL, 0, 0, ZGRAD_GROUND, 1000, nullptr, 0, 0, 0);

			SidebarExtension->Active_Tab().IsToRedraw = true;
        }

		Repair.Draw_Me(true);
		Sell.Draw_Me(true);
		Power.Draw_Me(true);
		Waypoint.Draw_Me(true);
		RedrawSidebar = true;
	}

	/*
	**	Draw the side strip elements by calling their respective draw functions.
	*/
	if (IsSidebarActive)
	{
	    SidebarExtension->Active_Tab().Draw_It(complete);
	}

	if (Repair.IsDrawn)
	{
		RedrawSidebar = true;
		Repair.IsDrawn = false;
	}

	if (Sell.IsDrawn)
	{
		RedrawSidebar = true;
		Sell.IsDrawn = false;
	}

	if (Power.IsDrawn)
	{
		RedrawSidebar = true;
		Power.IsDrawn = false;
	}

	if (Waypoint.IsDrawn)
	{
		RedrawSidebar = true;
		Waypoint.IsDrawn = false;
	}

	if (ToolTipHandler)
		ToolTipHandler->Force_Redraw(true);

	IsToRedraw = false;
	IsToFullRedraw = false;
	Blit_Sidebar(complete);
	TempSurface = oldsurface;
}


bool StripClassFake::_Scroll(bool up)
{
	if (up)
	{
		if (!TopIndex)
			return false;
		Scroller--;
	}
	else
	{
		if (TopIndex + SidebarClassExtension::Max_Visible() >= BuildableCount + BuildableCount % 2)
			return false;
		Scroller++;
	}

	return true;
}

bool StripClassFake::_Scroll_Page(bool up)
{
	if (up)
	{
		if (!TopIndex)
			return false;
		Scroller -= SidebarClassExtension::Max_Visible(true);
	}
	else
	{
		if (TopIndex + SidebarClassExtension::Max_Visible() >= BuildableCount + BuildableCount % 2)
			return false;
		Scroller += SidebarClassExtension::Max_Visible(true);
	}
	return true;
}


void StripClassFake::_One_Time(int id)
{
	DarkenShape = MFCC::RetrieveT<ShapeFileStruct>("DARKEN.SHP");
}


void StripClassFake::_Init_IO(int id)
{
	ID = id;

	UpButton[0].IsSticky = true;
	UpButton[0].ID = BUTTON_UP;
	UpButton[0].field_3C = true;
	UpButton[0].ShapeDrawer = SidebarDrawer;
	UpButton[0].Flags = GadgetClass::RIGHTRELEASE | GadgetClass::RIGHTPRESS | GadgetClass::LEFTRELEASE | GadgetClass::LEFTPRESS;

	DownButton[0].IsSticky = true;
	DownButton[0].ID = BUTTON_DOWN;
	DownButton[0].field_3C = true;
	DownButton[0].ShapeDrawer = SidebarDrawer;
	DownButton[0].Flags = GadgetClass::RIGHTRELEASE | GadgetClass::RIGHTPRESS | GadgetClass::LEFTRELEASE | GadgetClass::LEFTPRESS;

	int max_visible = SidebarClassExtension::Max_Visible();
	for (int index = 0; index < max_visible; index++)
	{
		SelectClass& g = SidebarExtension->SelectButton[ID][index];
		g.ID = BUTTON_SELECT;
		g.X = SidebarRect.X + ((index % 2 == 0) ? SidebarClass::COLUMN_ONE_X : SidebarClass::COLUMN_TWO_X);
		g.Y = SidebarRect.Y + SidebarClassExtension::COLUMN_ONE_Y + ((index / 2) * OBJECT_HEIGHT);
		g.Width = OBJECT_WIDTH;
		g.Height = OBJECT_HEIGHT;
		g.Set_Owner(*this, index);
	}
}


void StripClassFake::_Init_For_House(int id)
{
	UpButton[0].Set_Shape(MFCC::RetrieveT<ShapeFileStruct>("R-UP.SHP"), 0, 0);
	UpButton[0].ShapeDrawer = SidebarDrawer;

	DownButton[0].Set_Shape(MFCC::RetrieveT<ShapeFileStruct>("R-DN.SHP"), 0, 0);
	DownButton[0].ShapeDrawer = SidebarDrawer;
}


void StripClassFake::_Activate()
{
	UpButton[0].Zap();
	Map.Add_A_Button(UpButton[0]);

	DownButton[0].Zap();
	Map.Add_A_Button(DownButton[0]);

	int max_visible = SidebarClassExtension::Max_Visible();
	for (int index = 0; index < max_visible; index++)
	{
		SidebarExtension->SelectButton[ID][index].Zap();
		Map.Add_A_Button(SidebarExtension->SelectButton[ID][index]);
	}
}


void StripClassFake::_Deactivate()
{
	Map.Remove_A_Button(UpButton[0]);
	Map.Remove_A_Button(DownButton[0]);

	int max_visible = SidebarClassExtension::Max_Visible();
	for (int index = 0; index < max_visible; index++)
	{
		Map.Remove_A_Button(SidebarExtension->SelectButton[ID][index]);
	}
}


bool StripClassFake::_AI(KeyNumType& input, Point2D&)
{
	bool redraw = false;

	/*
	**	If this is scroll button for this side strip, then scroll the strip as
	**	indicated.
	*/
	if (input == (UpButton[0].ID | KN_BUTTON))
	{
		UpButton[0].IsPressed = false;
		if (!Scroll(true))
			Sound_Effect(Rule->ScoldSound);
	}
	if (input == (DownButton[0].ID | KN_BUTTON))
	{
		DownButton[0].IsPressed = false;
		if (!Scroll(false))
			Sound_Effect(Rule->ScoldSound);
	}

	/*
	**	Reflect the scroll desired direction/value into the scroll
	**	logic handler. This might result in up or down scrolling.
	*/
	if (!IsScrolling && Scroller)
	{
		if (BuildableCount <= SidebarClassExtension::Max_Visible())
		{
			Scroller = 0;
		}
		else
		{
			/*
			**	Top of list is moving toward lower ordered entries in the object list. It looks like
			**	the "window" to the object list is moving up even though the actual object images are
			**	scrolling downward.
			*/
			if (Scroller < 0)
			{
				if (!TopIndex)
				{
					Scroller = 0;
				}
				else
				{
					Scroller++;
					IsScrollingDown = false;
					IsScrolling = true;
					TopIndex -= 2;
					Slid = 0;
				}

			}
			else
			{
				if (TopIndex + SidebarClassExtension::Max_Visible() > BuildableCount)
				{
					Scroller = 0;
				}
				else
				{
					Scroller--;
					Slid = OBJECT_HEIGHT;
					IsScrollingDown = true;
					IsScrolling = true;
				}
			}
		}
	}

	/*
	**	Scroll logic is handled here.
	*/
	if (IsScrolling)
	{
		if (IsScrollingDown)
		{
			Slid -= SCROLL_RATE;
			if (Slid <= 0)
			{
				IsScrolling = false;
				Slid = 0;
				TopIndex += 2;
			}
		}
		else
		{
			Slid += SCROLL_RATE;
			if (Slid >= OBJECT_HEIGHT)
			{
				IsScrolling = false;
				Slid = 0;
			}
		}
		redraw = true;
	}

	/*
	**	Handle any flashing logic. Flashing occurs when the player selects an object
	**	and provides the visual feedback of a recognized and legal selection.
	*/
	if (Flasher != -1)
	{
		if (Graphic_Logic())
		{
			redraw = true;
			if (Fetch_Stage() >= 7)
			{
				Set_Rate(0);
				Set_Stage(0);
				Flasher = -1;
			}
		}
	}

	/*
	**	Handle any building clock animation logic.
	*/
	if (IsBuilding)
	{
		for (int index = 0; index < BuildableCount; index++)
		{
			FactoryClass* factory = Buildables[index].Factory;
			if (factory && factory->Has_Changed())
			{
				redraw = true;
				if (factory->Has_Completed())
				{
					/*
					**	Construction has been completed. Announce this fact to the player and
					**	try to get the object to automatically leave the factory. Buildings are
					**	the main exception to the ability to leave the factory under their own
					**	power.
					*/
					TechnoClass* pending = factory->Get_Object();
					if (pending != nullptr)
					{
						switch (pending->Kind_Of())
					    {
						case RTTI_UNIT:
						case RTTI_AIRCRAFT:
							OutList.Add(EventClass(pending->Owner(), EVENT_PLACE, pending->Kind_Of(), &INVALID_CELL));
						    Speak(VOX_UNIT_READY);
							break;

						case RTTI_BUILDING:
							Speak(VOX_CONSTRUCTION);
							break;

						case RTTI_INFANTRY:
							OutList.Add(EventClass(pending->Owner(), EVENT_PLACE, pending->Kind_Of(), &INVALID_CELL));
							Speak(VOX_UNIT_READY);
							break;

						default:
							break;
						}
					}
				}
			}
		}
	}

	/*
	**	If any of the logic determined that this side strip needs to be redrawn, then
	**	set the redraw flag for this side strip.
	*/
	if (redraw)
	{
		IsToRedraw = true;
		Flag_To_Redraw();
		RedrawSidebar = true;
	}

	return redraw;
}


bool StripClassFake::_Factory_Link(FactoryClass* factory, RTTIType type, int id)
{
	for (int i = 0; i < BuildableCount; i++)
	{
		if (Buildables[i].BuildableType == type &&
			Buildables[i].BuildableID == id)
		{
			Buildables[i].Factory = factory;
			IsBuilding = true;
			/*
			** Flag that all the icons on this strip need to be redrawn
			*/
			Flag_To_Redraw();
			return true;
		}
	}

	return false;
}


const char* StripClassFake::_Help_Text(int gadget_id)
{
	static char _buffer[512];

	int i = gadget_id + TopIndex;

	if (GameActive)
	{
		if (i < BuildableCount && BuildableCount < MAX_BUILDABLES)
		{
			if (Buildables[i].BuildableType == RTTI_SPECIAL)
				return SuperWeaponTypes[Buildables[i].BuildableID]->Full_Name();

			const TechnoTypeClass* ttype = Fetch_Techno_Type(Buildables[i].BuildableType, Buildables[i].BuildableID);

			// BUGFIX from YR.
			if (!ttype)
				return nullptr;

			const TechnoTypeClassExtension* technotypeext = Extension::Fetch<TechnoTypeClassExtension>(ttype);
			const char* description = technotypeext->Description;

			if (description[0] == '\0') {
				// If there is no extended description, then simply show the name and price.
				std::snprintf(_buffer, sizeof(_buffer), "%s@$%d", ttype->Full_Name(), ttype->Cost_Of(PlayerPtr));
			}
			else {
				// If there is an extended description, then show the name, price, and the description.
				std::snprintf(_buffer, sizeof(_buffer), "%s@$%d@@%s", ttype->Full_Name(), ttype->Cost_Of(PlayerPtr), technotypeext->Description);
			}

			/*if (Map.field_1CD4)
				std::snprintf(_buffer, sizeof(_buffer), Fetch_String(TXT_MONEY_FORMAT_1), ttype->Cost_Of(PlayerPtr));
			else
				std::snprintf(_buffer, sizeof(_buffer), Fetch_String(TXT_MONEY_FORMAT_2), ttype->Full_Name(), ttype->Cost_Of(PlayerPtr));*/

			return _buffer;
		}
	}

	return nullptr;
}

DECLARE_PATCH(_PowerClass_Draw_It_Bar_Count)
{
	GET_REGISTER_STATIC(int, bar_count, eax);

	bar_count += (SidebarClassExtension::COLUMN_ONE_Y - SidebarClass::COLUMN_ONE_Y) / 4;

	if (bar_count > 0)
	{
		// We have bars to draw, draw them
	    _asm mov edi, bar_count
		JMP(0x005AB50D);
	}

	// No bars to draw
	JMP(0x005AB550);
}


/**
 *  Main function for patching the hooks.
 */
void SidebarClassExtension_Hooks()
{
	Patch_Jump(0x005F23AC, &_SidebarClass_Constructor_Patch);
	Patch_Jump(0x005B8B7D, &_SidebarClass_Destructor_Patch);

	Patch_Jump(0x005F2610, &SidebarClassFake::_One_Time);
	Patch_Jump(0x005F2660, &SidebarClassFake::_Init_Clear);
	Patch_Jump(0x005F2720, &SidebarClassFake::_Init_IO);
	Patch_Jump(0x005F2900, &SidebarClassFake::_Init_For_House);
	Patch_Jump(0x005F2B00, &SidebarClassFake::_Init_Strips);
	Patch_Jump(0x005F2C30, &SidebarClassExtension::Which_Tab);
	Patch_Jump(0x005F2C50, &SidebarClassFake::_Factory_Link);
	Patch_Jump(0x005F2E20, &SidebarClassFake::_Add);
	Patch_Jump(0x005F2E90, &SidebarClassFake::_Scroll);
	Patch_Jump(0x005F30F0, &SidebarClassFake::_Scroll_Page);
	Patch_Jump(0x005F3560, &SidebarClassFake::_Draw_It);
	Patch_Jump(0x005F3C70, &SidebarClassFake::_AI);
	Patch_Jump(0x005F3E20, &SidebarClassFake::_Recalc);
	Patch_Jump(0x005F3E60, &SidebarClassFake::_Activate);
	Patch_Jump(0x005F5F70, &SidebarClassFake::_Abandon_Production);
	Patch_Jump(0x005F6080, &SidebarClassFake::_entry_84);
	Patch_Jump(0x005F6620, &SidebarClassFake::_Help_Text);
	Patch_Jump(0x005F6670, &SidebarClassFake::_Max_Visible);

	Patch_Jump(0x005F4210, &StripClassFake::_One_Time);
	Patch_Jump(0x005F42A0, &StripClassFake::_Init_IO);
	Patch_Jump(0x005F4450, &StripClassFake::_Activate);
	Patch_Jump(0x005F4560, &StripClassFake::_Deactivate);
	Patch_Jump(0x005F46B0, &StripClassFake::_Scroll);
	Patch_Jump(0x005F4760, &StripClassFake::_Scroll_Page);
	Patch_Jump(0x005F4910, &StripClassFake::_AI);
	Patch_Jump(0x005F4E40, &StripClassFake::_Help_Text);
	Patch_Jump(0x005F4F10, &StripClassFake::_Draw_It);
	Patch_Jump(0x005F5F10, &StripClassFake::_Factory_Link);

	Patch_Jump(0x005AB507, _PowerClass_Draw_It_Bar_Count);

    //Patch_Jump(0x005F5188, &_SidebarClass_StripClass_ObjectTypeClass_Custom_Cameo_Image_Patch);
    //Patch_Jump(0x005F5216, &_SidebarClass_StripClass_SuperWeaponType_Custom_Cameo_Image_Patch);
    //Patch_Jump(0x005F52AF, &_SidebarClass_StripClass_Custom_Cameo_Image_Patch);
	
    // NOP away tooltip length check for formatting
    Patch_Byte(0x0044E486, 0x90);
    Patch_Byte(0x0044E486 + 1, 0x90);

    // Change jle to jl to allow rendering tooltips that are exactly as wide as the sidebar
    Patch_Byte(0x0044E605 + 1, 0x8C);

    // Patch the argument to HouseClass::Can_Build in SidebarClass::StripClass::Recalc so that prerequisites are checked
    Patch_Byte(0x005F5762, false);
}
