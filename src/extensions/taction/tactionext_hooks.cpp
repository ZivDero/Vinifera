/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *
 *  @project       Vinifera
 *
 *  @file          TACTIONEXT_HOOKS.CPP
 *
 *  @author        CCHyper
 *
 *  @brief         Contains the hooks for the extended TriggerClass.
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
#include "tactionext_hooks.h"

#include <algorithm>
#include "tibsun_globals.h"
#include "tibsun_inline.h"
#include "trigger.h"
#include "triggertype.h"
#include "taction.h"
#include "scenario.h"
#include "scenarioext.h"
#include "voc.h"
#include "tactionext.h"
#include "taction.h"
#include "tibsun_defines.h"
#include "vinifera_defines.h"
#include "house.h"
#include "housetype.h"
#include "object.h"
#include "objecttype.h"
#include "trigger.h"
#include "triggertype.h"
#include "fatal.h"
#include "debughandler.h"
#include "asserthandler.h"
#include "house.h"
#include "housetype.h"
#include "session.h"
#include "voxellib.h"

#include "hooker.h"
#include "hooker_macros.h"
#include "mouse.h"
#include "rules.h"


/**
 *  A fake class for implementing new member functions which allow
 *  access to the "this" pointer of the intended class.
 *
 *  @note: This must not contain a constructor or destructor!
 *  @note: All functions must be prefixed with "_" to prevent accidental virtualization.
 */
DECLARE_EXTENDING_CLASS_AND_PAIR(TActionClass)
{
public:
    bool _Operator_Parens_Intercept(HouseClass* house, ObjectClass* object, TriggerClass* trigger, Cell const& cell);
};


/**
 *  Intercept for TActionClass::operator() to add the
 *  execution of our new TActions.
 *
 *  @author: ZivDero
 */
bool TActionClassExt::_Operator_Parens_Intercept(HouseClass* house, ObjectClass* object, TriggerClass* trigger, Cell const& cell)
{
    bool success = true;

    /**
     *  If this is a Vinifera TAction, execute it.
     */
    if (TActionClassExtension::Is_Vinifera_TAction(Action)) {
        success = TActionClassExtension::Execute(*this, house, object, trigger, cell);
    }

    /**
     *  Otherwise, let the game handle it.
     */
    else {
        success = TActionClass::operator()(house, object, trigger, cell);
    }

    return success;
}


struct VoxelShadowRenderStruct {
    VoxelLibraryClass* VoxLib;
    int Layer;
    int Info;
    Vector3 ShadowCorner[4];
};


VoxelShadowRenderStruct(&VoxelShadowRenderData)[64] = Make_Global<VoxelShadowRenderStruct[64]>(0x00832740);
int& VoxelShadowRenderDataCount = Make_Global<int>(0x00822338);
Vector3& MinVoxelBounds = Make_Global<Vector3>(0x0081FDA8);
Vector3& MaxVoxelBounds = Make_Global<Vector3>(0x00820110);


Vector3 Project_Onto_XY(Vector3 point, Vector3 direction)
{
    if (fabs(direction.Z) < 1e-6f) {
        // Parallel to XY plane
        return Vector3(point.X, point.Y, 0.0f);  // or just return original with Z=0?
    }

    direction = direction / direction.Length();
    float t = -point.Z / direction.Z;
    return point + direction * t;
}


void Prep_For_Shadow(VoxelLibraryClass* voxlib, int layer, int info, Matrix3D const& camera, Matrix3D const& motion, Vector3 const& light)
{
    VoxelLibraryClass::LayerInfoStruct const& layer_info = *voxlib->Get_Layer_Info(layer, info);

    VoxelShadowRenderStruct& data = VoxelShadowRenderData[VoxelShadowRenderDataCount];

    data.VoxLib = voxlib;
    data.Layer = layer;
    data.Info = info;

    for (int i = 0; i < 4; i++) {
#if 0 // this is for the vanilla flattened shadow but fixed
        data.ShadowCorner[i] = motion * (layer_info.Bounds[i] + light);
        data.ShadowCorner[i].Z = 0;
        data.ShadowCorner[i] = camera * data.ShadowCorner[i];
        data.ShadowCorner[i].Y = -data.ShadowCorner[i].Y;
#endif
        // First, transform bounding box corner to world space
        Vector3 world_corner = motion * layer_info.Bounds[i];

        // Project it onto the XY plane along light direction
        Vector3 projected = Project_Onto_XY(world_corner, light);

        // Apply camera transform and screen-space Y flip
        projected = camera * projected;
        projected.Y = -projected.Y;

        data.ShadowCorner[i] = projected;

        MinVoxelBounds.X = std::min(data.ShadowCorner[i].X, MinVoxelBounds.X);
        MinVoxelBounds.Y = std::min(data.ShadowCorner[i].Y, MinVoxelBounds.Y);
        MaxVoxelBounds.X = std::max(data.ShadowCorner[i].X, MaxVoxelBounds.X);
        MaxVoxelBounds.Y = std::max(data.ShadowCorner[i].Y, MaxVoxelBounds.Y);
    }

    VoxelShadowRenderDataCount++;
}


// disable the main voxel model so that it doesn't obscure our precious shadow
void Prep_For_Object(VoxelLibraryClass* voxlib, int layer, int info, Matrix3D const& transform)
{
    
}




/**
 *  Main function for patching the hooks.
 */
void TActionClassExtension_Hooks()
{
    Patch_Jump(0x006661C0, &Prep_For_Shadow);
    Patch_Jump(0x00666300, &Prep_For_Object);
    Patch_Call(0x0064961C, &TActionClassExt::_Operator_Parens_Intercept);

    /**
     *  #issue-674
     * 
     *  Fixes a bug where the game would crash when TACTION_WAKEUP_GROUP was
     *  executed but the game was not able to match the Group to the triggers
     *  group. This was because the game was searching the Foots vector with
     *  the count of the Technos vector, and in cases where the Group did
     *  not match, the game would crash trying to search out of bounds.
     * 
     *  @author: CCHyper
     */
    Patch_Dword(0x00619552+2, (0x007E4820+4)); // Foot vector to Technos vector.
}
