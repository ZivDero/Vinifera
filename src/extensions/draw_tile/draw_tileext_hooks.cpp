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

#include "debughandler.h"
#include "graphics_device.h"
#include "hooker.h"
#include "isotiletype.h"
#include "lightconvert.h"
#include "mouse.h"
#include "optionsext.h"
#include "palette_lut.h"
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
        bool b1, bool b2, bool b3,
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
    bool b1, bool b2, bool b3,
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
                          b1, b2, b3, grey_shift);
        return;
    }

    /**
     *  Resolve atlas + palette via process-wide caches. The IsoTileSet
     *  pointer comes from this->Get_Tile_Data(); LightConvertClass is itself
     *  a ConvertClass subclass so PaletteCache is keyed on its address.
     */
    GraphicsDevice& device = *Vinifera::Gfx::Device;
    const void* iso_tileset = static_cast<const void*>(this->Get_Tile_Data());
    TmpAsset* asset = TmpCache::Get().Get_Or_Load(device, iso_tileset);
    PaletteLUT* palette = PaletteCache::Get().Get_Or_Build(device, drawer);
    if (asset == nullptr || palette == nullptr) {
        Vanilla_Draw_Tile(this, drawer, tile_num, surface, x_off, y_off, cliprect,
                          cell_level, cell_color, use_z, cell_variation,
                          b1, b2, b3, grey_shift);
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

    /**
     *  Cell-screen depth: mirrors the sprite proxy's screen-Y normalization,
     *  with epsilon=0 (tiles are the depth floor). Cell elevation is already
     *  baked into y_off by CellClass::Draw_It.
     */
    float dz;
    {
        const float kMaxScreenY = 16000.0f;
        dz = 1.0f - ((float)y_off / kMaxScreenY);
        if (dz < 0.001f) dz = 0.001f;
        if (dz > 0.999f) dz = 0.999f;
    }

    /**
     *  Per-cell brightness modulate. cell_color is 1..256-ish in vanilla's
     *  scheme; passes through as a per-vertex multiplier on the palette
     *  output. Approximation: linear scale to [0..1].
     */
    int tint_v = cell_color;
    if (tint_v < 0) tint_v = 0;
    if (tint_v > 255) tint_v = 255;
    const uint32_t tint = (0xFFu << 24)
                        | ((uint32_t)tint_v << 16)
                        | ((uint32_t)tint_v << 8)
                        |  (uint32_t)tint_v;

    TileDrawCmd cmd = {};
    cmd.Asset        = asset;
    cmd.Palette      = palette;
    cmd.SubTileIndex = sub_index;
    cmd.Dst.X        = (float)(x_off + st->X) * xscale;
    cmd.Dst.Y        = (float)(y_off + st->Y) * yscale;
    cmd.Dst.W        = (float)st->W * xscale;
    cmd.Dst.H        = (float)st->H * yscale;
    cmd.DstZ         = dz;
    cmd.VertexTint   = tint;

    TileQueue::Get().Submit(cmd);

    (void)use_z;
    (void)cell_variation;
    (void)b1; (void)b2; (void)b3;
    (void)grey_shift;
    (void)cliprect;
    (void)cell_level;
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
