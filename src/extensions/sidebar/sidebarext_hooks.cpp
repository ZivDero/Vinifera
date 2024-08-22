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
	bool _Scroll(bool up, int column);
	bool _Activate(int control);
	void _Init_Strips();
	SidebarClassExtension::SidebarTabType _Which_Column(RTTIType type);
	bool _Factory_Link(FactoryClass* factory, RTTIType type, int id);
	bool _Add(RTTIType type, int id);
	void _Draw_It(bool complete);
	void _AI(KeyNumType& input, Point2D& xy);
	void _Recalc();
	bool _Abandon_Production(RTTIType type, FactoryClass* factory);
	const char* _Help_Text(int gadget_id);
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

    void _Draw_It(bool complete);
	bool _Scroll(bool up);
	void _Init_IO(int id);
	void _Activate();
	void _Deactivate();
	bool _AI(KeyNumType& input, Point2D& xy);
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
 *  Patch for including the extended class members when initialsing the scenario data.
 *
 *  @warning: Do not touch this unless you know what you are doing!
 *
 *  @author: ZivDero
 */
DECLARE_PATCH(_SidebarClass_Init_Clear_Patch)
{
	//GET_REGISTER_STATIC(SidebarClass*, this_ptr, esi);

	SidebarExtension->Init_Clear();

	/**
	 *  Stolen bytes here.
	 */
	_asm xor eax, eax

	JMP_REG(ecx, 0x005F2711)
}


/**
 *  Patch for including the extended class members when initialsing the scenario data.
 *
 *  @warning: Do not touch this unless you know what you are doing!
 *
 *  @author: ZivDero
 */
DECLARE_PATCH(_SidebarClass_Init_IO_Patch)
{
	//GET_REGISTER_STATIC(SidebarClass*, this_ptr, esi);

	SidebarExtension->Init_IO();

	JMP(0x005F28D1)
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


/**
 *  Adds support for extended sidebar tooltips.
 *
 *  @author: Rampastring
 */
char extended_description[512];
DECLARE_PATCH(_SidebarClass_StripClass_Help_Text_Extended_Tooltip_Patch)
{
    GET_REGISTER_STATIC(int, cost, eax);
    GET_REGISTER_STATIC(TechnoTypeClass*, technotype, esi);

    static TechnoTypeClassExtension* technotypeext;
    static char* description;
    technotypeext = Extension::Fetch<TechnoTypeClassExtension>(technotype);
    description = technotypeext->Description;

    // Using sprintf below will affect the stack, but the compiler should also clean it up,
    // so there should be no issue.
    if (description[0] == '\0') {
        // If there is no extended description, then simply show the name and price.
        sprintf(extended_description, "%s@$%d", technotype->FullName, cost);
    }
    else {
        // If there is an extended description, then show the name, price, and the description.
        sprintf(extended_description, "%s@$%d@@%s", technotype->FullName, cost, technotypeext->Description);
    }

    // Set up return value
    _asm { mov  eax, offset ds : extended_description }
    JMP_REG(ecx, 0x005F4EFF);
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
			int index = i + TopIndex / 2;
			int x = i % 2 == 0 ? SidebarClass::COLUMN_ONE_X : SidebarClass::COLUMN_TWO_X;
			int y = SidebarClass::COLUMN_ONE_Y + ((i / 2) * OBJECT_HEIGHT);

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
					ShapeFileStruct* shape;
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
	if (*(int*)0x007E492C)
		return false;

	bool scr = SidebarExtension->Column[SidebarExtension->TabIndex]->Scroll(up);

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
	    SidebarExtension->Column[SidebarExtension->TabIndex]->AI(input, newpoint);
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
    if (SidebarExtension->Column[SidebarExtension->TabIndex]->Recalc())
    {
        IsToRedraw = true;
		Flag_To_Redraw();
    }
}


bool SidebarClassFake::_Abandon_Production(RTTIType type, FactoryClass* factory)
{
    return SidebarExtension->Column[_Which_Column(type)]->Abandon_Production(factory);
}


const char* SidebarClassFake::_Help_Text(int gadget_id)
{
	const char* text = PowerClass::Help_Text(gadget_id);
	if (text == nullptr)
	{
		int column = (gadget_id - 1000) >> 8;
		if (column < SidebarClassExtension::SIDEBAR_TAB_COUNT)
			return SidebarExtension->Column[column]->Help_Text((gadget_id - 1000) & 0xFF);
	}
	return text;
}


bool SidebarClassFake::_Activate(int control)
{
	bool old = IsSidebarActive;

	if (Session.Play && Session.Type != GAME_NORMAL && Session.Type != GAME_SKIRMISH)
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
			SidebarExtension->Column[SidebarExtension->TabIndex]->Activate();
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


SidebarClassExtension::SidebarTabType SidebarClassFake::_Which_Column(RTTIType type)
{
    switch (type)
    {
    case RTTI_BUILDINGTYPE:
    case RTTI_BUILDING:
        return SidebarClassExtension::SIDEBAR_TAB_STRUCTURE;

    default:
		return SidebarClassExtension::SIDEBAR_TAB_UNIT;
    }

}


bool SidebarClassFake::_Factory_Link(FactoryClass* factory, RTTIType type, int id)
{
    SidebarClassExtension::SidebarTabType column = _Which_Column(type);
	for (int i = 0; i < SidebarExtension->Column[column]->BuildableCount; i++)
	{
	    if (SidebarExtension->Column[column]->Buildables[i].BuildableType == type &&
			SidebarExtension->Column[column]->Buildables[i].BuildableID == id)
	    {
			SidebarExtension->Column[column]->Buildables[i].Factory = factory;
			SidebarExtension->Column[column]->IsBuilding = true;
			SidebarExtension->Column[column]->IsToRedraw = true;
			Map.Flag_To_Redraw();
            return true;
	    }
	}

	return false;
}


bool SidebarClassFake::_Add(RTTIType type, int id)
{
	if (!Debug_Map)
	{
		SidebarClassExtension::SidebarTabType column = _Which_Column(type);

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
		if (complete || SidebarExtension->Column[SidebarExtension->TabIndex]->IsToRedraw)
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

			SidebarExtension->Column[SidebarExtension->TabIndex]->IsToRedraw = true;
        }
	}

	/*
	**	Draw the side strip elements by calling their respective draw functions.
	*/
	if (IsSidebarActive)
	{
	    SidebarExtension->Column[SidebarExtension->TabIndex]->Draw_It(complete);
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
		Scroller -= 8;
	}
	else
	{
		// We want to make sure the last odd item can be shown
		int countToShow = BuildableCount + BuildableCount % 2;

		if (TopIndex + SidebarClassExtension::Max_Visible() >= countToShow)
			return false;
		Scroller += 8;
	}

	return true;
}


void StripClassFake::_Init_IO(int id)
{
	ID = id;

	UpButton[0].IsSticky = true;
	UpButton[0].ID = BUTTON_UP + id;
	UpButton[0].field_3C = true;
	UpButton[0].ShapeDrawer = SidebarDrawer;
	UpButton[0].Flags = GadgetClass::RIGHTRELEASE | GadgetClass::RIGHTPRESS | GadgetClass::LEFTRELEASE | GadgetClass::LEFTPRESS;

	DownButton[0].IsSticky = true;
	DownButton[0].ID = BUTTON_UP + id;
	DownButton[0].field_3C = true;
	DownButton[0].ShapeDrawer = SidebarDrawer;
	DownButton[0].Flags = GadgetClass::RIGHTRELEASE | GadgetClass::RIGHTPRESS | GadgetClass::LEFTRELEASE | GadgetClass::LEFTPRESS;

	int max_visible = SidebarClassExtension::Max_Visible();
	for (int index = 0; index < max_visible; index++)
	{
		SelectClass& g = SidebarExtension->SelectButton[ID][index];
		g.ID = BUTTON_SELECT;
		g.X = SidebarRect.X + ((index % 2 == 0) ? SidebarClass::COLUMN_ONE_X : SidebarClass::COLUMN_TWO_X);
		g.Y = SidebarRect.Y + SidebarClass::COLUMN_ONE_Y + ((index / 2) * OBJECT_HEIGHT);
		g.Width = OBJECT_WIDTH;
		g.Height = OBJECT_HEIGHT;
		g.Set_Owner(*this, index);
	}
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
		Map.Remove_A_Button(SelectButton[ID][index]);
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
					TopIndex--;
					Slid = 0;
				}

			}
			else
			{
				if (TopIndex + SidebarClassExtension::Max_Visible() >= BuildableCount)
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
				TopIndex++;
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


DECLARE_PATCH(_SidebarClass_entry_84_Patch)
{
	SidebarExtension->Entry_84_Tooltips();

	JMP(0x005F65B5);
}


/**
 *  Main function for patching the hooks.
 */
void SidebarClassExtension_Hooks()
{
	Patch_Jump(0x005F23AC, &_SidebarClass_Constructor_Patch);
	Patch_Jump(0x005B8B7D, &_SidebarClass_Destructor_Patch);
	Patch_Jump(0x005F2683, &_SidebarClass_Init_Clear_Patch);
	Patch_Jump(0x005F28B8, &_SidebarClass_Init_IO_Patch);
	Patch_Jump(0x005F620D, &_SidebarClass_entry_84_Patch);

	Patch_Jump(0x005F3E60, &SidebarClassFake::_Activate);
	Patch_Jump(0x005F2B00, &SidebarClassFake::_Init_Strips);
	Patch_Jump(0x005F2C30, &SidebarClassFake::_Which_Column);
	Patch_Jump(0x005F2C50, &SidebarClassFake::_Factory_Link);
	Patch_Jump(0x005F2E20, &SidebarClassFake::_Add);
	Patch_Jump(0x005F2E90, &SidebarClassFake::_Scroll);
	//Patch_Jump(0x005F30F0, &SidebarClassFake::_Page);
	Patch_Jump(0x005F3560, &SidebarClassFake::_Draw_It);
	Patch_Jump(0x005F3C70, &SidebarClassFake::_AI);
	Patch_Jump(0x005F3E20, &SidebarClassFake::_Recalc);
	Patch_Jump(0x005F5F70, &SidebarClassFake::_Abandon_Production);
	Patch_Jump(0x005F6620, &SidebarClassFake::_Help_Text);

	Patch_Jump(0x005F42A0, &StripClassFake::_Init_IO);
	Patch_Jump(0x005F4450, &StripClassFake::_Activate);
	Patch_Jump(0x005F4560, &StripClassFake::_Deactivate);
	Patch_Jump(0x005F4F10, &StripClassFake::_Draw_It);
	Patch_Jump(0x005F46B0, &StripClassFake::_Scroll);
	Patch_Jump(0x005F4910, &StripClassFake::_AI);

    //Patch_Jump(0x005F5188, &_SidebarClass_StripClass_ObjectTypeClass_Custom_Cameo_Image_Patch);
    //Patch_Jump(0x005F5216, &_SidebarClass_StripClass_SuperWeaponType_Custom_Cameo_Image_Patch);
    //Patch_Jump(0x005F52AF, &_SidebarClass_StripClass_Custom_Cameo_Image_Patch);
    //Patch_Jump(0x005F4EDD, &_SidebarClass_StripClass_Help_Text_Extended_Tooltip_Patch);
    //Patch_Byte(0x005F4EF7 + 2, 0x14); // Pop one more argument passed to sprintf
	//
    //// NOP away tooltip length check for formatting
    //Patch_Byte(0x0044E486, 0x90);
    //Patch_Byte(0x0044E486 + 1, 0x90);
	//

    // Change jle to jl to allow rendering tooltips that are exactly as wide as the sidebar
    Patch_Byte(0x0044E605 + 1, 0x8C);

    // Patch the argument to HouseClass::Can_Build in SidebarClass::StripClass::Recalc so that prerequisites are checked
    Patch_Byte(0x005F5762, false);

	
	//Patch_Jump(0x005F6396, &_SidebarClass_entry_84_SelectClass_Positions);
	//Patch_Jump(0x005F634D, &_SidebarClass_entry_84_SelectClass_Count);
	//Patch_Jump(0x005F44CC, &_StripClass_Activate_SelectClass_Count);
	//Patch_Jump(0x005F45B2, &_StripClass_Deactivate_SelectClass_Count);
	//Patch_Jump(0x005F4380, &_StripClass_Init_IO_SelectClass_Count);
	//Patch_Jump(0x005F3818, &_SidebarClass_Draw_It_Draw_Tab);
	//Patch_Jump(0x005F3F39, 0x005F3F44); //SidebarClass::Activate skip activating the second tab
}
