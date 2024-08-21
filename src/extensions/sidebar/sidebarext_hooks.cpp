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

#include "hooker.h"
#include "hooker_macros.h"
#include "sidebar.h"
#include "tibsun_functions.h"
#include "house.h"
#include "language.h"
#include "super.h"
#include "techno.h"
#include "textprint.h"


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
    //void _Verify_Can_Build();
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
};


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
    if (technotypeext->CameoImageSurface) {
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
    if (supertypeext->CameoImageSurface) {
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

		//SidebarRedraws++;

		/*
		**	Fills the background to the side strip. We shouldnt need to do this if the strip
		** has a full complement of icons.
		*/
		/*
		** New sidebar needs to be drawn not filled
		*/
		/*if (BuildableCount < MAX_VISIBLE) {
			CC_Draw_Shape(LogoShapes, ID, X + (2 * RESFACTOR), Y, WINDOW_MAIN, SHAPE_WIN_REL | SHAPE_NORMAL, 0);
		}*/

		/*
		**	Redraw the scroll buttons.
		*/
		UpButton[ID].Draw_Me(true);
		DownButton[ID].Draw_Me(true);


		int maxvisible;
		if (SidebarSurface && SidebarClass::SidebarShape)
		{
			maxvisible = (SidebarRect.Height - SidebarClass::SidebarBottomShape->Get_Height() - SidebarClass::SidebarShape->Get_Height()) /
				SidebarClass::SidebarMiddleShape->Get_Height();
		}
		else
		{
			maxvisible = MAX_VISIBLE;
		}

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
			//int shapenum = 0;
			//void const* remapper = 0;
			FactoryClass* factory = 0;
			int index = i + TopIndex;
			int x = X;
			int y = /*Y*/26 + (i * OBJECT_HEIGHT);

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
						**	Fetch the remap table that is appropriate for this object
						**	type.
						*/
						//remapper = PlayerPtr->Remap_Table(false, ((TechnoTypeClass const*)obj)->Remap);

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

						/*
						**	Infantry don't get remapped in the sidebar (special case).
						*/
						//if (Buildables[index].BuildableType == RTTI_INFANTRYTYPE) {
						//	remapper = 0;
						//}

						shapefile = obj->Get_Cameo_Data();
						//shapenum = 0;

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
							// jump away

							/*
							**	Darken the imagery if a factory of a matching type is
							**	already busy.
							*/
							//darken = isbusy;
						}
					}
					else {
						shapefile = LogoShape;
						//darken = PlayerPtr->Is_Hack_Prevented(Buildables[index].BuildableType, Buildables[index].BuildableID);
					}

				}
				else
				{
					spc = (SpecialWeaponType)Buildables[index].BuildableID;
					SuperWeaponTypeClass* swtype = SuperWeaponTypes[spc];

					name = SuperWeaponTypes[spc]->FullName;
					shapefile = Get_Special_Cameo(spc);
					//shapenum = 0;

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

			//remapper = 0;
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
						total > 0 && factory->Object == nullptr ||
						factory->Object->Techno_Type_Class() != nullptr && factory->Object->Techno_Type_Class() != obj)
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

	if (UpButton[ID].IsDrawn)
	{
		RedrawSidebar = true;
		UpButton[ID].IsDrawn = false;
	}

	if (DownButton[ID].IsDrawn)
	{
		RedrawSidebar = true;
		DownButton[ID].IsDrawn = false;
	}
}


/**
 *  Main function for patching the hooks.
 */
void SidebarClassExtension_Hooks()
{
    Patch_Jump(0x005F5188, &_SidebarClass_StripClass_ObjectTypeClass_Custom_Cameo_Image_Patch);
    Patch_Jump(0x005F5216, &_SidebarClass_StripClass_SuperWeaponType_Custom_Cameo_Image_Patch);
    Patch_Jump(0x005F52AF, &_SidebarClass_StripClass_Custom_Cameo_Image_Patch);
    Patch_Jump(0x005F4EDD, &_SidebarClass_StripClass_Help_Text_Extended_Tooltip_Patch);
    Patch_Byte(0x005F4EF7 + 2, 0x14); // Pop one more argument passed to sprintf

    // NOP away tooltip length check for formatting
    Patch_Byte(0x0044E486, 0x90);
    Patch_Byte(0x0044E486 + 1, 0x90);

    // Change jle to jl to allow rendering tooltips that are exactly as wide as the sidebar
    Patch_Byte(0x0044E605 + 1, 0x8C);

    // Patch the argument to HouseClass::Can_Build in SidebarClass::StripClass::Recalc so that prerequisites are checked
    Patch_Byte(0x005F5762, false);
}
