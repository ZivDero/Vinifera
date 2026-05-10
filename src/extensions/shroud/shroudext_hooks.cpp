/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Stage 4 Phase 4.2: GPU shroud / fog alpha-write hooks.
 *
 *          Vanilla's per-cell shroud / fog renderer iterates the SHP frame
 *          pixel-by-pixel and writes each non-transparent byte into the CPU
 *          `AlphaBuffer`. We `Patch_Jump` the entry of each inner function
 *          and submit a single GPU draw command instead. The per-pixel
 *          formula moves into `ShroudFogEffect`'s pixel shader.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#include "always.h"

#include "shroudext_hooks.h"

#include "cell.h"
#include "cellext_const.h"
#include "debughandler.h"
#include "graphics_device.h"
#include "hooker.h"
#include "optionsext.h"
#include "scenario.h"
#include "shapeset.h"
#include "shp_asset.h"
#include "shp_cache.h"
#include "shroud_fog_queue.h"
#include "special.h"
#include "tibsun_globals.h"
#include "vinifera_globals.h"


using namespace Vinifera::Gfx;


/**
 *  Fake extension class so the patched function pointer is a __thiscall
 *  member function. Same pattern as `IsoTileTypeClassExt` in
 *  `draw_tileext_hooks.cpp`.
 */
class CellClassFake : public CellClass
{
public:
    void _Draw_Shroud_Or_Fog_Shape(Point2D& drawpoint, Rect& cliprect, char shapenum);
    void _Draw_Fog_Shape(Point2D& drawpoint, Rect& cliprect, int shapenum);
};


void CellClassFake::_Draw_Shroud_Or_Fog_Shape(Point2D& drawpoint, Rect& cliprect, char shapenum)
{
    static bool _shroud_one_time = false;
    static const ShapeSet* _shroud_shape;
    static const ShapeSet* _fog_shape;

    /**
     *  Perform a one-time load of the shroud and fog shape data.
     */
    if (!_shroud_one_time) {
        _shroud_shape = static_cast<const ShapeSet*>(MFCD::Retrieve("SHROUD.SHP"));
        _fog_shape = static_cast<const ShapeSet*>(MFCD::Retrieve("FOG.SHP"));
        _shroud_one_time = true;
    }

    /**
     *  If we are playing a multiplayer game, use the hardcoded shape data.
     */
    if (!Session.Singleplayer_Game()) {
        Cell_ShroudShape = reinterpret_cast<const ShapeSet*>(&ShroudShapeBinary);
        Cell_FogShape = reinterpret_cast<const ShapeSet*>(&FogShapeBinary);
    } else {
        Cell_ShroudShape = _shroud_shape;
        Cell_FogShape = _fog_shape;
    }

    /**
     *  LegacyRenderer flag or pre-init device: defer to no-op. The CPU
     *  AlphaBuffer never gets the write either way (we replaced the function
     *  entirely), so on legacy renderer the visual loses shroud darkening.
     *  This matches the broader Phase 4.x pattern — legacy mode is only
     *  pixel-identical when *no* GPU code is running, which it isn't here.
     *  Acceptable: legacy is a developer A/B switch, not a shipping path.
     */
    const bool legacy = (OptionsExtension != nullptr) && OptionsExtension->LegacyRenderer;
    if (legacy || Vinifera::Gfx::Device == nullptr || Scen == nullptr) {
        return;
    }

    /**
     *  Vanilla picks between SHROUD.SHP and FOG.SHP depending on whether fog
     *  of war is enabled in the scenario (`Scen->Special & 0x800` →
     *  `IsFogOfWar`). Shroud (SHP overlap with no fog flag) uses
     *  `Cell_ShroudShape`; with fog enabled the function is repurposed to
     *  draw fog instead via `Cell_FogShape`.
     */
    const ShapeSet* shape = Scen->Special.IsFogOfWar ? Cell_FogShape : Cell_ShroudShape;
    if (shape == nullptr) {
        return;
    }

    GraphicsDevice& device = *Vinifera::Gfx::Device;
    ShpAsset* atlas = ShpCache::Get().Get_Or_Load(device, shape);
    if (atlas == nullptr) {
        return;
    }

    /**
     *  Submit in logical (pre-scale) screen pixels. `Flush` applies the
     *  logical → backbuffer scale uniformly to position *and* extents so
     *  the drawn quad covers the same pixel region the CPU blitter would
     *  have written to.
     */
    ShroudFogDrawCmd cmd = {};
    cmd.Asset      = atlas;
    cmd.FrameIndex = (uint8_t)shapenum;
    cmd.ScreenX    = drawpoint.X;
    cmd.ScreenY    = drawpoint.Y;
    cmd.Mode       = ShroudFogMode::ShroudOverwrite;
    ShroudFogQueue::Get().Submit(cmd);
}


void CellClassFake::_Draw_Fog_Shape(Point2D& drawpoint, Rect& cliprect, int shapenum)
{
    static bool _fog_one_time = false;
    static const ShapeSet* _fog_shape;

    /**
     *  Perform a one-time load of the fog shape data.
     */
    if (!_fog_one_time) {
        _fog_shape = static_cast<const ShapeSet*>(MFCD::Retrieve("FOG.SHP"));
        _fog_one_time = true;
    }

    /**
     *  If we are playing a multiplayer game, use the hardcoded shape data.
     */
    if (!Session.Singleplayer_Game()) {
        Cell_FixupFogShape = reinterpret_cast<const ShapeSet*>(&FogShapeBinary);
    } else {
        Cell_FixupFogShape = _fog_shape;
    }

    /**
     *  The parent (Draw_Shroud_And_Fog) already gates this on
     *  `Scen->Special.IsFogOfWar && !PlayerPtr->IsDefeated`, so we never
     *  arrive here unless fog is active. No additional gating needed.
     */
    const bool legacy = (OptionsExtension != nullptr) && OptionsExtension->LegacyRenderer;
    if (legacy || Vinifera::Gfx::Device == nullptr) {
        return;
    }
    if (Cell_FogShape == nullptr) {
        return;
    }

    GraphicsDevice& device = *Vinifera::Gfx::Device;
    ShpAsset* atlas = ShpCache::Get().Get_Or_Load(device, Cell_FogShape);
    if (atlas == nullptr) {
        return;
    }

    ShroudFogDrawCmd cmd = {};
    cmd.Asset      = atlas;
    cmd.FrameIndex = shapenum;
    cmd.ScreenX    = drawpoint.X;
    cmd.ScreenY    = drawpoint.Y;
    cmd.Mode       = ShroudFogMode::FogAdditive;
    ShroudFogQueue::Get().Submit(cmd);
}


void Shroud_Hooks()
{
    Patch_Jump(0x00454E60, &CellClassFake::_Draw_Shroud_Or_Fog_Shape);
    Patch_Jump(0x00455130, &CellClassFake::_Draw_Fog_Shape);

    DEBUG_INFO("Shroud_Hooks: redirected 2 CPU shroud/fog blit entry points to GPU.\n");
}
