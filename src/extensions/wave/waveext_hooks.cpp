/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Contains the hooks for the extended WaveClass.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#include "always.h"

#include "waveext_hooks.h"

#include "graphics_device.h"
#include "hooker.h"
#include "hooker_macros.h"
#include "options.h"
#include "polygon.h"
#include "rect.h"
#include "tibsun_globals.h"
#include "vinifera_globals.h"
#include "wave.h"
#include "wave_queue.h"
#include "waveext_init.h"


/**
 *  Map vanilla's 8-direction facing (`Direction`, a `FacingType`) to a 2D
 *  pixel-cell perpendicular delta. Mirrors `DirectionStrides` which stores
 *  1D row-offsets; we project those back to (dx, dy). Used by Sonic to
 *  displace the SceneCopy sample perpendicular to the beam.
 */
static const Point2D kPerpDirs[8] = {
    {  0, -1 },  // N
    {  1, -1 },  // NE
    {  1,  0 },  // E
    {  1,  1 },  // SE
    {  0,  1 },  // S
    { -1,  1 },  // SW
    { -1,  0 },  // W
    { -1, -1 },  // NW
};


/**
 *  Replacement class. ABI-compatible with vanilla `WaveClass`. Used as the
 *  Patch_Jump target for `Draw_Sonic` and `Draw_Laser`.
 */
class WaveClassExt : public WaveClass
{
public:
    void _Draw_Sonic(Point2D& xy, Rect& bounds) const;
    void _Draw_Laser(Point2D& xy, Rect& bounds) const;
};


/**
 *  Convert the WaveClass active-polygon state to a GPU draw cmd and submit
 *  it to the WaveQueue. The 6 polygon vertices come from `WaveShape.Vertices`
 *  (vanilla sets this to point at `&ActiveWaveStartMiddle`, so the 6
 *  contiguous Point2D fields ARE the vertex array indexed by
 *  `PolygonShapeStruct::END_LEFT..START_LEFT`). Vertices live in scene-local
 *  pixel space; the offset `dxy = xy - WaveStartMiddle` shifts them to
 *  scene-RT coords, matching vanilla's per-pixel `+ (xy.X - WaveStartMiddle.X,
 *  xy.Y - WaveStartMiddle.Y)` translation.
 */
/**
 *  Scene RT is sized to vanilla's full LogicalSurface resolution (including
 *  the top tabs.shp bar and any sidebar) while `WaveShape.Vertices` comes
 *  from `Coord_To_Pixel` which returns TACTICAL-RELATIVE coords. Convert
 *  via `+ TacticalRect.TopLeft` so the wave lands at the right place on
 *  screen. Vanilla's `Draw_Sonic` does the same with the `+ TacticalRect.Y`
 *  it adds to the Y blit offset.
 */
static inline Point2D Tactical_To_Scene_RT(int x, int y)
{
    return Point2D(x + TacticalRect.X, y + TacticalRect.Y);
}


void WaveClassExt::_Draw_Sonic(Point2D& xy, Rect& /*bounds*/) const
{
    if (Vinifera::Gfx::Device == nullptr) return;
    if (!Vinifera::Gfx::WaveQueue::Get().Is_Initialized()) return;
    if (WaveShape.Vertices == nullptr) return;

    const Point2D dxy(xy.X - WaveStartMiddle.X, xy.Y - WaveStartMiddle.Y);

    Vinifera::Gfx::WaveDrawCmd cmd{};
    cmd.Kind = Vinifera::Gfx::WaveKind::Sonic;
    for (int i = 0; i < 6; ++i) {
        cmd.Vertices[i] = Tactical_To_Scene_RT(
            WaveShape.Vertices[i].X + dxy.X,
            WaveShape.Vertices[i].Y + dxy.Y);
    }
    cmd.PerpDir   = kPerpDirs[(int)Direction & 7];
    cmd.RadiusRef = Tactical_To_Scene_RT(WaveStartMiddle.X + dxy.X,
                                         WaveStartMiddle.Y + dxy.Y);
    cmd.SonicEC   = SonicEC;
    cmd.LaserMult = 0;

    Vinifera::Gfx::WaveQueue::Get().Submit(cmd);
}


void WaveClassExt::_Draw_Laser(Point2D& xy, Rect& /*bounds*/) const
{
    /**
     *  Vanilla gates Laser rendering on DetailLevel == 2 (highest). Preserve
     *  that — modders / players on lower detail should still see no laser.
     */
    if (Options.DetailLevel != 2) return;
    if (Vinifera::Gfx::Device == nullptr) return;
    if (!Vinifera::Gfx::WaveQueue::Get().Is_Initialized()) return;
    if (WaveShape.Vertices == nullptr) return;

    const Point2D dxy(xy.X - WaveStartMiddle.X, xy.Y - WaveStartMiddle.Y);

    Vinifera::Gfx::WaveDrawCmd cmd{};
    cmd.Kind = Vinifera::Gfx::WaveKind::Laser;
    for (int i = 0; i < 6; ++i) {
        cmd.Vertices[i] = Tactical_To_Scene_RT(
            WaveShape.Vertices[i].X + dxy.X,
            WaveShape.Vertices[i].Y + dxy.Y);
    }
    cmd.PerpDir   = Point2D(0, 0);   // laser doesn't warp
    cmd.SonicEC   = 0;
    cmd.LaserMult = LaserEC;         // vanilla passes LaserEC straight in as `mult`

    Vinifera::Gfx::WaveQueue::Get().Submit(cmd);
}


/**
 *  Main function for patching the hooks.
 */
void WaveClassExtension_Hooks()
{
    /**
     *  Initialises the extended class.
     */
    WaveClassExtension_Init();

    /**
     *  Replace the vanilla CPU rasterisers for sonic and laser beams with
     *  our GPU queue submission. Both vanilla functions lock the
     *  LogicalSurface and write pixels directly — useless under our GPU
     *  pipeline since the surface isn't backed by a CPU-readable buffer.
     */
    Patch_Jump(0x00670F10, &WaveClassExt::_Draw_Sonic);
    Patch_Jump(0x006715F0, &WaveClassExt::_Draw_Laser);
}
