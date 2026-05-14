/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Replaces `CellClass::Draw_It` with a GPU-friendly variant that
 *          submits one `TileDrawCmd` per cell against a shared palette
 *          (looked up via `PaletteCache` from the active `PaletteClass*` —
 *          today `IsoTilePalette`) plus the tile-effect-owned tint mask.
 *          Per-cell lighting
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
#include "extension_globals.h"
#include "graphics_device.h"
#include "hooker.h"
#include "mouse.h"
#include "iso_tile_asset.h"
#include "isotiletype.h"
#include "render_pass.h"
#include "rulesext.h"
#include "shp_cache.h"
#include "smudgetype.h"
#include "surface.h"
#include "syringe.h"
#include "tibsun_globals.h"
#include "tibsun_inline.h"
#include "tile_queue.h"
#include "vinifera_globals.h"


using namespace Vinifera::Gfx;


/**
 *  Function-pointer to vanilla `IsometricTileTypeClass::Draw_Tile`. Used only
 *  for the device-not-ready fall-through inside our `Draw_It` reimpl; the
 *  function entry stays unpatched.
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
 *  Pack a cell's lighting tuple into RGBA: tint colours / 1000 (so 1.0 is
 *  neutral) plus `TileBrightness` / 1000 in the alpha lane.
 */
static void Pack_Cell_Tint(const CellClass* cell, float out[4])
{
    out[0] = (float)cell->RedTint        / 1000.0f;
    out[1] = (float)cell->GreenTint      / 1000.0f;
    out[2] = (float)cell->BlueTint       / 1000.0f;
    out[3] = (float)cell->TileBrightness / 1000.0f;
}


/**
 *  Read a neighbour cell's tint in screen-N/E/S/W direction (the cells
 *  whose diamonds share an edge with `cell`). Falls back to `own` if the
 *  neighbour is off the playable map.
 *
 *  In TS iso, the cells visually adjacent to `cell` on screen are *not*
 *  the cardinal TS-FACING neighbours — the iso projection rotates the
 *  cell grid 45° relative to the screen. Mapping:
 *    screen-NE = FACING_N, screen-SE = FACING_E,
 *    screen-SW = FACING_S, screen-NW = FACING_W,
 *    screen-N  = FACING_NW, screen-E  = FACING_NE,
 *    screen-S  = FACING_SE, screen-W  = FACING_SW.
 */
static void Sample_Neighbour(const CellClass* cell, FacingType face,
                             const float own[4], float out[4])
{
    const Cell neighbour_pos = Adjacent_Cell(cell->CellID, face);
    if (Map.In_Radar(neighbour_pos)) {
        Pack_Cell_Tint(&Map[neighbour_pos], out);
    } else {
        out[0] = own[0]; out[1] = own[1]; out[2] = own[2]; out[3] = own[3];
    }
}


/**
 *  Compute the tint at a diamond-corner vertex by averaging the four
 *  cells that meet at that screen point: `cell`, the two cardinal
 *  neighbours whose diamond edges end at the corner, and the diagonal
 *  neighbour whose opposite-side diamond corner sits there.
 *
 *  Mapping per diamond corner:
 *    iso-diamond N corner (rect top middle):    cell + FACING_N  + FACING_W  + FACING_NW
 *    iso-diamond E corner (rect right middle):  cell + FACING_N  + FACING_E  + FACING_NE
 *    iso-diamond S corner (rect bottom middle): cell + FACING_E  + FACING_S  + FACING_SE
 *    iso-diamond W corner (rect left middle):   cell + FACING_S  + FACING_W  + FACING_SW
 *
 *  Because all four cells that share a diamond corner compute the same
 *  four-cell average, the per-cell tints agree at the corner and
 *  linearly interpolated values agree along the entire shared edge —
 *  no visible cell-grid seam.
 */
static void Corner_Tint(const CellClass* cell,
                        FacingType card_a, FacingType card_b, FacingType diag,
                        const float own[4], float out[4])
{
    float a[4], b[4], d[4];
    Sample_Neighbour(cell, card_a, own, a);
    Sample_Neighbour(cell, card_b, own, b);
    Sample_Neighbour(cell, diag,   own, d);
    out[0] = (own[0] + a[0] + b[0] + d[0]) * 0.25f;
    out[1] = (own[1] + a[1] + b[1] + d[1]) * 0.25f;
    out[2] = (own[2] + a[2] + b[2] + d[2]) * 0.25f;
    out[3] = (own[3] + a[3] + b[3] + d[3]) * 0.25f;
}


/**
 *  Submit a tile cell's terrain icon to the GPU queue. Packs the cell's
 *  lighting (RedTint / GreenTint / BlueTint / TileBrightness) into the
 *  diamond-centre vertex and the four cardinal-neighbour-averaged values
 *  into the diamond corners; the tile shader interpolates linearly across
 *  the fan. When `[AudioVisual] SmoothLighting` is off all five are set
 *  to the cell's own value and `TileQueue::Flush_Pass` falls back to a
 *  uniform quad.
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

    /**
     *  Tiles are drawn into SceneRT which is sized to vanilla's logical
     *  resolution, so logical coords pass through unscaled. The present
     *  quad upscales SceneRT → Backbuffer for the display.
     */
    const float xscale = 1.0f;
    const float yscale = 1.0f;

    const Surface* surface = LogicalSurface;
    const Rect surface_rect = (surface != nullptr) ? surface->Get_Rect() : Rect(0, 0, VideoWidth, VideoHeight);
    const Rect clipped_rect = Intersect(cliprect, surface_rect);
    if (!clipped_rect.Is_Valid()) {
        return;
    }

    float own_tint[4];
    Pack_Cell_Tint(cell, own_tint);

    const bool smooth_lighting = (RuleExtension != nullptr) && RuleExtension->IsSmoothLighting;

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
    memcpy(cmd.TintC, own_tint, sizeof(own_tint));
    if (smooth_lighting) {
        Corner_Tint(cell, FACING_N, FACING_W, FACING_NW, own_tint, cmd.TintN);
        Corner_Tint(cell, FACING_N, FACING_E, FACING_NE, own_tint, cmd.TintE);
        Corner_Tint(cell, FACING_E, FACING_S, FACING_SE, own_tint, cmd.TintS);
        Corner_Tint(cell, FACING_S, FACING_W, FACING_SW, own_tint, cmd.TintW);
    } else {
        memcpy(cmd.TintN, own_tint, sizeof(own_tint));
        memcpy(cmd.TintE, own_tint, sizeof(own_tint));
        memcpy(cmd.TintS, own_tint, sizeof(own_tint));
        memcpy(cmd.TintW, own_tint, sizeof(own_tint));
    }
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
             *  Subtile records flagged `Is_Randomized` (bit 2 of
             *  `IsoTileRecord::Flags`) drive their variation off the cell's
             *  `IsBridgeEndDamaged` bit, not the spatial `Clear_Icon` hash.
             *  This applies to bridge END pieces (`BridgeSet`/`TrainBridgeSet`)
             *  AND bridge MIDDLE spans (`BridgeMiddle1/2`) — a bridge tile
             *  set has exactly two entries (intact / damaged) and the same
             *  `IsBridgeEndDamaged` bit flips the whole span in lockstep.
             */
            if (ittype->Is_Randomized(SubTile)) {
                icon = IsBridgeEndDamaged ? 1 : 0;
            } else {
                icon = const_cast<CellClassExt*>(this)->Clear_Icon(ITType, ittype->TilesInSequence);
            }
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

    if (ittype->Get_Tile_Data() != nullptr && Vinifera::Gfx::Device != nullptr) {
        Point2D p = drawpoint + Point2D(0, TacticalRect.Y);
        Submit_Tile_GPU(this, ittype, subtile, p.X, p.Y, cliprect, Height, icon);
    }

    if (Smudge != SMUDGE_NONE) {
        SmudgeTypes[Smudge]->Draw_It(drawpoint + Point2D(ISO_TILE_PIXEL_W / 2, TacticalRect.Y) - cliprect.TopLeft, cliprect, SmudgeData, LEVEL_LEPTON_H * Height, CellID);
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
 *  `IsometricTileTypeClass::Read_Control_File` reloads the theater's
 *  IsoTilePalette + all tile sets. Clear `PaletteCache` so the next
 *  `Get_Or_Build` rebuilds with the fresh palette content; otherwise we'd
 *  keep serving stale colors after a theater swap. SHP / voxel / font
 *  palettes also get invalidated — those `ConvertClass` instances are
 *  rebuilt against the new theater anyway.
 *
 *  Entry instruction `sub esp, 91Ch` (81 EC 1C 09 00 00) — 6 bytes.
 */
DEFINE_HOOK(0x004F39E0, _Read_Control_File_Clear_Palette_Cache, 6)
{
    PaletteCache::Get().Clear();
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
