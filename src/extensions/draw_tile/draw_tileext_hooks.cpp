/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Replaces `CellClass::Draw_It` with a GPU-friendly variant that
 *          submits one `TileDrawCmd` per cell against the single global
 *          `IsoTilePaletteRes` palette + tint mask. Per-cell lighting
 *          (RedTint / GreenTint / BlueTint / TileBrightness) rides in the
 *          vertex color attribute and is unfolded by the tile shader.
 *
 *          Stops intercepting `IsometricTileTypeClass::Draw_Tile` — vanilla's
 *          CPU rasterizer is now unreachable from the GPU path. The
 *          `Draw_Clear_Tile` proxy is also dropped; per-cell z-buffer
 *          pre-clear is redundant against the GPU depth-target frame clear.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#include "always.h"

#include "draw_tileext_hooks.h"

#include "cell.h"
#include "debughandler.h"
#include "graphics_device.h"
#include "hooker.h"
#include "mouse.h"
#include "iso_tile_asset.h"
#include "iso_tile_palette.h"
#include "isotiletype.h"
#include "optionsext.h"
#include "render_pass.h"
#include "shp_cache.h"
#include "smudgetype.h"
#include "surface.h"
#include "syringe.h"
#include "tibsun_globals.h"
#include "tile_queue.h"
#include "vinifera_globals.h"


using namespace Vinifera::Gfx;


/**
 *  Function-pointer to vanilla `IsometricTileTypeClass::Draw_Tile`. Used only
 *  for the `LegacyRenderer` / device-not-ready fall-through inside our
 *  `Draw_It` reimpl; the function entry stays unpatched.
 */
typedef void (__thiscall *VanillaDrawTileFn)(IsometricTileTypeClass*,
    LightConvertClass*, int, Surface&, int, int, Rect, int, int,
    bool, int, bool, bool, bool, signed int);
static const VanillaDrawTileFn Vanilla_Draw_Tile =
    reinterpret_cast<VanillaDrawTileFn>(0x004F6630);


static float Tile_Base_Depth_From_Visual_Y(int y_off, int cell_level, int tile_height)
{
    const float kMaxScreenY = 16000.0f;
    const int level_draw_pixels = (LEVEL_PIXEL_H_1 > 0) ? LEVEL_PIXEL_H_1 : 12;
    const float depth_y = (float)(y_off + tile_height + cell_level * level_draw_pixels);

    float dz = 1.0f - (depth_y / kMaxScreenY);
    if (dz < 0.001f) dz = 0.001f;
    if (dz > 0.999f) dz = 0.999f;
    return dz;
}


static IsometricTileTypeClass* Resolve_Tile_Variation(IsometricTileTypeClass* isotype, int cell_variation)
{
    if (isotype == nullptr || cell_variation == 0) {
        return isotype;
    }

    const int sequence_count = isotype->TilesInSequence;
    if (sequence_count <= 1) {
        return isotype;
    }

    if (cell_variation > sequence_count - 1) {
        cell_variation %= sequence_count;
    }
    if (cell_variation == 0) {
        return isotype;
    }

    IsometricTileTypeClass* varied_type = isotype;
    while (cell_variation-- > 0 && varied_type != nullptr) {
        varied_type = varied_type->NextTileTypeInSet;
    }

    return (varied_type != nullptr) ? varied_type : isotype;
}


/**
 *  Submit a tile cell's terrain icon to the GPU queue. Reads tint/brightness
 *  directly off `cell` (matching what vanilla's `Init_Drawer` writes back into
 *  CellClass), packs them into the 4-float vertex color attribute, and lets
 *  the tile shader unfold the AlphaLightingRemap math at pixel time.
 */
static void Submit_Tile_GPU(const CellClass* cell, IsometricTileTypeClass* ittype,
                            int subtile, int x_off, int y_off,
                            Rect const& cliprect, int cell_level, int cell_variation)
{
    GraphicsDevice& device = *Vinifera::Gfx::Device;
    IsometricTileTypeClass* draw_type = Resolve_Tile_Variation(ittype, cell_variation);
    if (draw_type == nullptr) {
        return;
    }
    const void* iso_tileset = static_cast<const void*>(draw_type->Get_Tile_Data());
    IsoTileAsset* asset = IsoTileCache::Get().Get_Or_Load(device, iso_tileset);
    if (asset == nullptr) {
        return;
    }

    int sub_index = subtile;
    if (sub_index < 0) {
        sub_index = 0;
    } else if (asset->Sub_Tile_Count() > 0) {
        sub_index %= asset->Sub_Tile_Count();
    }
    const IsoTileSubTileInfo* st = asset->Get_Sub_Tile(sub_index);
    if (st == nullptr || st->W <= 0 || st->H <= 0) {
        return;
    }

    const float xscale = (VideoWidth > 0) ? (float)device.Get_Backbuffer_Width()  / (float)VideoWidth  : 1.0f;
    const float yscale = (VideoHeight > 0) ? (float)device.Get_Backbuffer_Height() / (float)VideoHeight : 1.0f;

    const Surface* surface = LogicalSurface;
    const Rect surface_rect = (surface != nullptr) ? surface->Get_Rect() : Rect(0, 0, VideoWidth, VideoHeight);
    const Rect clipped_rect = Intersect(cliprect, surface_rect);
    if (!clipped_rect.Is_Valid()) {
        return;
    }

    /**
     *  Per-cell lighting. RedTint / GreenTint / BlueTint / TileBrightness are
     *  vanilla's 0..2000-range values (1000 = neutral); the shader divides
     *  `cell_color = v.col.a * 1000` to recover TileBrightness for the
     *  AlphaLightingRemap formula.
     */
    const float tint_r      = (float)cell->RedTint        / 1000.0f;
    const float tint_g      = (float)cell->GreenTint      / 1000.0f;
    const float tint_b      = (float)cell->BlueTint       / 1000.0f;
    const float brightness  = (float)cell->TileBrightness / 1000.0f;

    const float dz = Tile_Base_Depth_From_Visual_Y(y_off, cell_level, st->H);

    TileDrawCmd cmd = {};
    cmd.Asset        = asset;
    cmd.SubTileIndex = sub_index;
    cmd.Dst.X        = (float)x_off * xscale;
    cmd.Dst.Y        = (float)y_off * yscale;
    cmd.Dst.W        = (float)st->W * xscale;
    cmd.Dst.H        = (float)st->H * yscale;
    cmd.Clip.X       = (float)clipped_rect.X * xscale;
    cmd.Clip.Y       = (float)clipped_rect.Y * yscale;
    cmd.Clip.W       = (float)clipped_rect.Width * xscale;
    cmd.Clip.H       = (float)clipped_rect.Height * yscale;
    cmd.DstZTop      = dz;
    cmd.DstZBottom   = dz;
    cmd.Pass         = Current_Render_Pass();
    cmd.Tint[0]      = tint_r;
    cmd.Tint[1]      = tint_g;
    cmd.Tint[2]      = tint_b;
    cmd.Tint[3]      = brightness;
    cmd.DrawExtra    = false;

    TileQueue::Get().Submit(cmd);

    /**
     *  Cliffs / walls / ramp bodies live in the per-record extra rect.
     *  Vanilla blits this on top of the base diamond at offset
     *  (record->ExtraX, record->ExtraY) — typically negative Y for cliffs
     *  that extend upward. Same lighting; same base Z; per-pixel cliff/body
     *  depth comes from `ExtraZOffset`.
     */
    if (st->HasExtraData && st->ExtraW > 0 && st->ExtraH > 0) {
        TileDrawCmd extra_cmd = cmd;
        extra_cmd.Dst.X     = (float)(x_off + st->ExtraX - st->X) * xscale;
        extra_cmd.Dst.Y     = (float)(y_off + st->ExtraY - st->Y) * yscale;
        extra_cmd.Dst.W     = (float)st->ExtraW * xscale;
        extra_cmd.Dst.H     = (float)st->ExtraH * yscale;
        extra_cmd.DrawExtra = true;
        TileQueue::Get().Submit(extra_cmd);
    }
}


/**
 *  Fake extension class for `Patch_Jump` on `CellClass::Draw_It` (entry @
 *  0x004564D0). Vanilla signature: `void (Point2D const&, Rect const&, bool) const`.
 *
 *  @note: must not contain ctor/dtor/virtuals.
 */
class CellClassExt : public CellClass
{
public:
    void _Draw_It(Point2D const& xdrawpoint, Rect const& cliprect, bool objects) const;
};


void CellClassExt::_Draw_It(Point2D const& xdrawpoint, Rect const& cliprect, bool objects) const
{
    /**
     *  Vanilla's `Draw_It` only acts in the `!objects` branch — the "objects"
     *  pass is owned by other code paths that walk the cell's occupiers.
     */
    if (objects) {
        return;
    }

    /**
     *  Vanilla init-on-first-use: cells with no drawer get a neutral one.
     *  Const-cast matches vanilla's own `((CellClass*)this)->Init_Drawer(NULL)`.
     */
    if (Drawer == nullptr) {
        const_cast<CellClassExt*>(this)->Init_Drawer(nullptr);
    }

    /**
     *  Inlined `Fetch_Icon` (vanilla `cell.cpp:2286`) — picks the iso-tile
     *  type, subtile, and variation icon for this cell.
     */
    IsometricTileTypeClass* ittype = nullptr;
    int subtile = 0;
    int icon = 0;
    if (ITType != ISOTILE_NONE) {
        ittype = IsoTileTypes[ITType];
        subtile = SubTile;
        if (ittype != nullptr && ittype->TilesInSequence > 1) {
            /**
             *  Vanilla checks `Is_Randomized(SubTile)` to choose between
             *  bridge-damage-driven and `Clear_Icon`-driven variation. We
             *  always use `Clear_Icon` here — `Is_Randomized` isn't exported
             *  by TSpp and the bridge-damage path is a minor visual nuance
             *  on a single set of tiles. TODO: wire it up.
             */
            icon = const_cast<CellClassExt*>(this)->Clear_Icon(ITType, ittype->TilesInSequence);
        }
    } else {
        ittype = IsoTileTypes[ISOTILE_CLEAR];
        if (ittype != nullptr && ittype->TilesInSequence > 1) {
            icon = const_cast<CellClassExt*>(this)->Clear_Icon(ISOTILE_CLEAR, ittype->TilesInSequence);
        }
    }
    if (ittype == nullptr) {
        return;
    }

    Point2D drawpoint = xdrawpoint;
    drawpoint.Y -= LEVEL_PIXEL_H_1 * Height;

    if (ittype->Get_Tile_Data() != nullptr) {
        Point2D p = drawpoint + Point2D(0, TacticalRect.Y);
        const bool legacy = (OptionsExtension != nullptr) && OptionsExtension->LegacyRenderer;
        if (legacy || Vinifera::Gfx::Device == nullptr) {
            Vanilla_Draw_Tile(ittype, Drawer, subtile, *LogicalSurface, p.X, p.Y,
                              cliprect, Height, TileBrightness,
                              true, icon, false, false, false, 0);
        } else {
            Submit_Tile_GPU(this, ittype, subtile, p.X, p.Y, cliprect, Height, icon);
        }
    }

    if (Smudge != SMUDGE_NONE) {
        Point2D smudge_pt = drawpoint + Point2D(ISO_TILE_PIXEL_W / 2, TacticalRect.Y) - cliprect.TopLeft;
        Rect smudge_clip = cliprect;
        SmudgeTypes[Smudge]->Draw_It(smudge_pt, smudge_clip, SmudgeData,
                                     LEVEL_LEPTON_H * Height,
                                     const_cast<Cell&>(CellID));
    }
}


/**
 *  Force a full-map redraw on every frame. Was previously installed by the
 *  CPU tile path; kept here since terrain redraws are still required for
 *  smudge / shroud overlays under the GPU pipeline.
 */
DEFINE_HOOK(0x004B95C6, _GScrenClass_Render_Draw_Flags_Zero, 5)
{
    Map.DrawFlags = GS_REDRAW_ALL;
    return 0;
}


/**
 *  Hook installer. Replaces `CellClass::Draw_It`'s function entry with our
 *  reimpl — no per-callsite `Patch_Call` plumbing, no `Draw_Clear_Tile`
 *  proxy (z-buffer is cleared per-frame on the GPU side).
 */
void DrawTile_Hooks()
{
    Patch_Jump(0x004564D0, &CellClassExt::_Draw_It);
    DEBUG_INFO("DrawTile_Hooks: installed CellClass::Draw_It replacement.\n");
}
