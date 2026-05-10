/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Stage 3: per-callsite interception of vanilla
 *          IsometricTileTypeClass::Draw_Tile.
 *
 *          Each Patch_Call entry rewrites a single CALL 0x004F6630 inside the
 *          original TS binary so it lands in our `IsoTileTypeClassExt::_Draw_Tile`
 *          instead. The proxy decides per-call whether to fall through to
 *          vanilla CPU rendering (LegacyRenderer flag set, or surface is not
 *          CompositeSurface) or to translate the call into a TileDrawCmd
 *          and Submit() it to the per-frame queue.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#include "always.h"

#include "draw_tileext_hooks.h"

#include "brightness.h"
#include "debughandler.h"
#include "graphics_device.h"
#include "hooker.h"
#include "isotiletype.h"
#include "lightconvert.h"
#include "mouse.h"
#include "optionsext.h"
#include "palette_lut.h"
#include "render_pass.h"
#include "shp_cache.h"
#include "surface.h"
#include "syringe.h"
#include "tibsun_globals.h"
#include "tile_queue.h"
#include "tmp_asset.h"
#include "vinifera_globals.h"


using namespace Vinifera::Gfx;


/**
 *  Function-pointer to the vanilla IsometricTileTypeClass::Draw_Tile (its
 *  entry at 0x004F6630 stays unpatched — we only rewrite individual CALL
 *  instructions, not the function entry, so calling through this thunk
 *  reaches vanilla without infinite recursion).
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
 *  Fake extension class so we can declare a member function with __thiscall
 *  semantics. Patch_Call extracts the code-pointer half of the member-pointer.
 *
 *  @note: must not contain ctor/dtor/virtuals.
 */
class IsoTileTypeClassExt : public IsometricTileTypeClass
{
public:
    void _Draw_Tile(
        LightConvertClass* drawer,
        int tile_num,
        Surface& surface,
        int x_off, int y_off,
        Rect cliprect,
        int cell_level,
        int cell_color,
        bool use_z,
        int cell_variation,
        bool solid_mask,
        bool z_clear_only,
        bool fog_mask,
        signed int grey_shift);
};


void IsoTileTypeClassExt::_Draw_Tile(
    LightConvertClass* drawer,
    int tile_num,
    Surface& surface,
    int x_off, int y_off,
    Rect cliprect,
    int cell_level,
    int cell_color,
    bool use_z,
    int cell_variation,
    bool solid_mask,
    bool z_clear_only,
    bool fog_mask,
    signed int grey_shift)
{
    /**
     *  Fall-through cases:
     *    - LegacyRenderer flag set
     *    - GraphicsDevice not initialized
     *    - bad inputs
     *
     *  We do NOT check the target surface: vanilla passes `LogicalSurface`,
     *  which during the tile pass is set to `TileSurface` (not the
     *  CompositeSurface the sprite proxy expects). Tile draws are inherently
     *  world-related; intercepting all of them is correct.
     */
    const bool legacy = (OptionsExtension != nullptr) && OptionsExtension->LegacyRenderer;
    if (legacy
        || Vinifera::Gfx::Device == nullptr
        || drawer == nullptr)
    {
        Vanilla_Draw_Tile(this, drawer, tile_num, surface, x_off, y_off, cliprect,
                          cell_level, cell_color, use_z, cell_variation,
                          solid_mask, z_clear_only, fog_mask, grey_shift);
        return;
    }

    /**
     *  Vanilla uses these booleans for non-standard tile blits:
     *    solid_mask:   solid grey/masked footprint + z write
     *    z_clear_only: z-buffer footprint clear only (CellClass::Draw_Clear_Tile)
     *    fog_mask:     fog/shroud-style grey/checker mask
     *
     *  The normal terrain pass is all-false. For z_clear_only, there is no color draw
     *  to preserve, and the DX11 depth buffer starts clear each frame, so the
     *  correct Stage-3 behavior is to avoid submitting a visible tile. Keep
     *  the visible special modes on vanilla until they have a GPU equivalent.
     */
    if (z_clear_only && !solid_mask && !fog_mask) {
        (void)use_z;
        (void)grey_shift;
        return;
    }
    if (solid_mask || fog_mask) {
        Vanilla_Draw_Tile(this, drawer, tile_num, surface, x_off, y_off, cliprect,
                          cell_level, cell_color, use_z, cell_variation,
                          solid_mask, z_clear_only, fog_mask, grey_shift);
        return;
    }

    /**
     *  Resolve atlas + palette via process-wide caches. The IsoTileSet
     *  pointer comes from this->Get_Tile_Data(); LightConvertClass is itself
     *  a ConvertClass subclass so PaletteCache is keyed on its address.
     */
    GraphicsDevice& device = *Vinifera::Gfx::Device;
    IsometricTileTypeClass* draw_type = Resolve_Tile_Variation(this, cell_variation);
    const void* iso_tileset = static_cast<const void*>(draw_type->Get_Tile_Data());
    TmpAsset* asset = TmpCache::Get().Get_Or_Load(device, iso_tileset);
    PaletteLUT* palette = PaletteCache::Get().Get_Or_Build(device, drawer);
    if (asset == nullptr || palette == nullptr) {
        Vanilla_Draw_Tile(this, drawer, tile_num, surface, x_off, y_off, cliprect,
                          cell_level, cell_color, use_z, cell_variation,
                          solid_mask, z_clear_only, fog_mask, grey_shift);
        return;
    }

    /**
     *  Wrap the sub-tile index with the count, mirroring vanilla's behavior
     *  (Fetch_Record_Pointer applies index % Tile_Count()).
     */
    int sub_index = tile_num;
    if (sub_index < 0) {
        sub_index = 0;
    } else if (asset->Sub_Tile_Count() > 0) {
        sub_index %= asset->Sub_Tile_Count();
    }
    const TmpSubTileInfo* st = asset->Get_Sub_Tile(sub_index);
    if (st == nullptr || st->W <= 0 || st->H <= 0) {
        return;
    }

    /**
     *  Logical → backbuffer-pixel scale, same as the sprite proxy.
     */
    const float xscale = (VideoWidth > 0) ? (float)device.Get_Backbuffer_Width()  / (float)VideoWidth  : 1.0f;
    const float yscale = (VideoHeight > 0) ? (float)device.Get_Backbuffer_Height() / (float)VideoHeight : 1.0f;

    const Rect clipped_rect = Intersect(cliprect, surface.Get_Rect());
    if (!clipped_rect.Is_Valid()) {
        return;
    }

    /**
     *  Vanilla seeds one base Z value per tile from the visually-raised
     *  y_off, then subtracts another half-cell-height per cell_level. Since
     *  CellClass::Draw_It already raised y_off by LEVEL_PIXEL_H_1 * level,
     *  those terms cancel back to the unraised cell plane. Per-pixel terrain
     *  shape comes from TMP ZData / ExtraZOffset, not from a whole-cell level
     *  band or a synthetic vertex gradient.
     */
    const float dz = Tile_Base_Depth_From_Visual_Y(y_off, cell_level, st->H);

    /**
     *  Per-cell brightness modulate. Vanilla `cell_color` (mis-named — it's
     *  the cell's TileBrightness) is in the 0..2000 range with 1000 = full
     *  normal and 2000 = max overbright. Brightness_To_Tint converts that
     *  to a [0, 2] linear RGB multiplier; the float vertex tint preserves
     *  values above 1.0 through to the shader.
     */
    const float tint_rgb = Brightness_To_Tint(cell_color);

    TileDrawCmd cmd = {};
    cmd.Asset        = asset;
    cmd.Palette      = palette;
    cmd.SubTileIndex = sub_index;
    /*
     * Vanilla draws the base 48x24 diamond at the cell drawpoint. The TMP
     * record X/Y fields describe where this sub-tile sits when composing the
     * whole multi-cell TMP; applying them again here shifts occupied cells
     * away from their map positions and opens gaps between sub-tiles.
     */
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
    cmd.Tint[0]      = tint_rgb;
    cmd.Tint[1]      = tint_rgb;
    cmd.Tint[2]      = tint_rgb;
    cmd.Tint[3]      = 1.0f;
    cmd.DrawExtra    = false;

    TileQueue::Get().Submit(cmd);

    /**
     *  Cliffs / walls / ramp bodies live in the per-record extra rect.
     *  vanilla blits this on top of the base diamond at offset
     *  (record->ExtraX, record->ExtraY) — typically negative Y for cliffs
     *  that extend upward. Render as a separate quad with the same base Z;
     *  ExtraZOffset supplies the per-pixel cliff/body depth.
     */
    if (st->HasExtraData && st->ExtraW > 0 && st->ExtraH > 0) {
        TileDrawCmd extra_cmd = cmd;
        extra_cmd.Dst.X     = (float)(x_off + st->ExtraX - st->X) * xscale;
        extra_cmd.Dst.Y     = (float)(y_off + st->ExtraY - st->Y) * yscale;
        extra_cmd.Dst.W     = (float)st->ExtraW * xscale;
        extra_cmd.Dst.H     = (float)st->ExtraH * yscale;
        extra_cmd.DrawExtra = true;
        /**
         *  Cliffs draw on top of the base ground at the same cell — bias Z
         *  slightly closer than the base so depth-test resolves correctly.
         */
        const float extra_dz = Tile_Base_Depth_From_Visual_Y(y_off, cell_level, st->H);
        extra_cmd.DstZTop = extra_dz;
        extra_cmd.DstZBottom = extra_dz;

        TileQueue::Get().Submit(extra_cmd);
    }

    (void)use_z;
    (void)grey_shift;
}


DEFINE_HOOK(0x004B95C6, _GScrenClass_Render_Draw_Flags_Zero, 5)
{
    Map.DrawFlags = Map.DrawFlags == GS_REDRAW_NONE ? GS_REDRAW_TACTICAL : GS_REDRAW_ALL;
    return 0;
}


/**
 *  Hook installer. Patch_Call rewrites each CALL 0x004F6630 instruction in
 *  the original binary to land in our proxy. Addresses come from IDA's xrefs
 *  to Draw_Tile.
 */
void DrawTile_Hooks()
{
    Patch_Call(0x0045635E, &IsoTileTypeClassExt::_Draw_Tile);  // CellClass::Draw_Clear_Tile
    Patch_Call(0x0045661A, &IsoTileTypeClassExt::_Draw_Tile);  // CellClass::Draw_It

    DEBUG_INFO("DrawTile_Hooks: installed 2 Draw_Tile callsite intercepts.\n");
}
