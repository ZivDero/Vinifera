/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Per-unit scratch render target for composite-unit rendering.
 *
 *          Multi-section voxel units suffer from "depth-blend compounding" when
 *          rendered directly to the scene RT. This scratch RT avoids it: all
 *          sections render opaque into the scratch (depth test picks the
 *          front-most voxel), then `End_Unit_Composite` blits the finished
 *          scratch to the scene as a single alpha-blended quad.
 *
 *          One scratch is shared across all units in a frame (rendered
 *          sequentially, with cheap clear between). Logical scratch coords
 *          are unit-local — voxel (0,0,0) lands at the scratch origin (128,
 *          128) by convention, so the section's projection T0 is offset by
 *          `(scratch_origin - drawpoint)` at submit time.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#pragma once

#include <cstdint>
#include <d3d11.h>

#include "effect.h"
#include "point.h"
#include "sprite_batch.h"


namespace Vinifera::Gfx
{
    class GraphicsDevice;
    class RenderTarget2D;


    /**
     *  Logical scratch size — large enough for any TS unit (Mammoth Mk II is
     *  ~80x80 visible; predator/walker units fit comfortably). Doubled from
     *  vanilla's 160x160 EightBitSurface to give effects headroom. Voxel
     *  submit-time transforms (T0/T1/T2/T3) are expressed in these logical
     *  units; the SSAA factor (below) is applied inside the queue, not at
     *  submit time.
     */
    constexpr int kUnitScratchWidth  = 256;
    constexpr int kUnitScratchHeight = 256;

    /**
     *  Logical origin within the scratch — voxel (0,0,0) of the unit lands
     *  here. Section T0 is shifted by `(origin - unit_drawpoint)` at submit
     *  so all sections project into the scratch with the unit's drawpoint
     *  collapsed to this fixed local point.
     */
    constexpr int kUnitScratchOriginX = kUnitScratchWidth  / 2;
    constexpr int kUnitScratchOriginY = kUnitScratchHeight / 2;
    inline const Point2D kUnitScratchOrigin(kUnitScratchOriginX, kUnitScratchOriginY);

    /**
     *  Backing-storage SSAA factor — the color + depth textures are
     *  allocated once at `(kUnitScratchWidth × kUnitScratchHeight)`
     *  multiplied by this. The *active* SSAA per Begin_Unit may be lower
     *  (currently gated by `[AudioVisual] SmoothVoxels=` — 2 when on,
     *  1 when off). When the active factor is less than the max, the
     *  unit renders into the top-left `(W*active × H*active)` region
     *  of the backing texture and the composite blit reads only that
     *  region. Keeping max=2 even when active=1 lets us toggle the
     *  rule live without reallocating the scratch.
     *
     *  2 is the right default — 4× would quadruple PS cost across the
     *  whole unit footprint for a barely-perceptible improvement at
     *  256² logical.
     */
    constexpr int kUnitScratchMaxSSAA      = 2;
    constexpr int kUnitScratchBackingWidth  = kUnitScratchWidth  * kUnitScratchMaxSSAA;
    constexpr int kUnitScratchBackingHeight = kUnitScratchHeight * kUnitScratchMaxSSAA;


    /**
     *  Effect that samples the scratch RT and resolves it to scene size.
     *  The PS does an explicit 4-tap SSAA resolve with a majority-opaque
     *  rule (see unit_composite.hlsl) — keeps silhouettes pixel-aligned
     *  while smoothing interior color transitions.
     *
     *  Bind layout:
     *    t0 — scratch RT SRV (backing-size, top-left active region)
     *    s0 — point-clamp sampler (unused; PS uses Load())
     *    b0 — SpriteCB (ProjMtx from SpriteBatch)
     *    b1 — UnitCompositeCB (alpha + active SSAA factor)
     */
    class UnitCompositeEffect : public Effect
    {
    public:
        struct Params
        {
            float Alpha;
            int   Ssaa;     // active SSAA stride for the 4-tap PS (1 → 1-tap pass-through; 2 → 2×2 block resolve)
            float _Pad[2];
        };

        bool Initialize(GraphicsDevice& device);
        void Shutdown();

        void Set_Params(GraphicsDevice& device, const Params& params);

    private:
        ID3D11Buffer* ParamsCB = nullptr;
    };


    class UnitScratch
    {
    public:
        static UnitScratch& Get();

        bool Initialize(GraphicsDevice& device);
        void Shutdown();

        bool Is_Initialized() const { return Initialized; }

        /**
         *  Active SSAA factor for the current Begin_Unit/End_Unit_Composite
         *  scope. Set by Begin_Unit from `[AudioVisual] SmoothVoxels=`
         *  (1 when off, 2 when on). Read by `VoxelQueue::Issue_Cmd_To_Scratch`
         *  and `SpriteQueue::Render_Sprite_To_Scratch_Immediate` to size
         *  their screen-space scaling, and by `End_Unit_Composite` to size
         *  the composite blit's source rect + Ssaa CB field.
         */
        int Get_Active_SSAA()   const { return CurrentSsaa; }
        int Get_Active_Width()  const { return kUnitScratchWidth  * CurrentSsaa; }
        int Get_Active_Height() const { return kUnitScratchHeight * CurrentSsaa; }

        /**
         *  Bind the scratch RTV+DSV with the active SSAA viewport
         *  (Get_Active_Width × Get_Active_Height), clear color to
         *  transparent and depth to 1.0. Saves the active RT/DSV
         *  bindings so `End_Unit_Composite` can restore them. The
         *  active SSAA factor is sampled from RulesExtension here, so
         *  toggling `[AudioVisual] SmoothVoxels=` takes effect on the
         *  next unit without recreating the scratch.
         */
        bool Begin_Unit(GraphicsDevice& device);

        /**
         *  Clear the scratch depth buffer to 1.0 without disturbing the
         *  scratch color RT. Used between captured records inside a single
         *  unit's composite so each record's voxel/SHP draws start from a
         *  fresh depth — gives strict painter's-order layering between
         *  records (body, voxel barrel, turret) while preserving per-voxel
         *  depth resolution within a single record's section.
         */
        void Clear_Depth(GraphicsDevice& device);

        /**
         *  Restore the scene RTV/DSV/viewport, then composite the scratch
         *  contents as a single quad. `scene_origin` is where the scratch's
         *  (0, 0) corner lands in scene-RT pixels — for unit-local rendering
         *  centered at the unit's drawpoint, pass `drawpoint - scratch_origin`.
         *  `alpha` is the visual-character translucency [0..1]. `scene_depth`
         *  is the depth emitted by every pixel of the quad (one value per
         *  unit, anchored at the unit's drawpoint Y to match tile depth).
         */
        void End_Unit_Composite(GraphicsDevice& device,
                                Point2D scene_origin,
                                float alpha,
                                float scene_depth);

        ID3D11ShaderResourceView* Get_SRV() const;

    private:
        UnitScratch() = default;

        bool Ensure_Targets(GraphicsDevice& device);
        void Release_Targets();

        /**
         *  SSAA color target — voxels render here at
         *  `kUnitScratchBackingWidth × kUnitScratchBackingHeight`. Single-
         *  sample so it can be bound as a regular SRV at composite time
         *  without a resolve step. When the active SSAA is less than the
         *  max (rule-toggled SmoothVoxels=off case), only the top-left
         *  `Get_Active_Width × Get_Active_Height` region is rendered and
         *  read; the remainder of the backing texture is leftover from
         *  prior frames and gets ignored by the composite blit's src rect.
         */
        RenderTarget2D*           ScratchRT  = nullptr;

        /**
         *  SSAA depth — same backing resolution as the color RT (required
         *  by D3D11 to bind both at OM). Single-sample D32_FLOAT. Consumed
         *  only inside the scratch render; never resolved or sampled.
         */
        ID3D11Texture2D*          DepthTex   = nullptr;
        ID3D11DepthStencilView*   DepthDSV   = nullptr;

        SpriteBatch               CompositeBatch;
        UnitCompositeEffect       CompositeFx;

        bool                      Initialized = false;

        /**
         *  Active SSAA factor for the current Begin_Unit scope. Refreshed
         *  from `RuleExtension->IsSmoothVoxels` on each Begin_Unit. Default
         *  matches the max so the first frame before a unit is rendered
         *  still has a sane value.
         */
        int                       CurrentSsaa = kUnitScratchMaxSSAA;

        /**
         *  Saved bindings restored by `End_Unit_Composite`. Captured at
         *  `Begin_Unit` time. We hold owning refs (AddRef) so a binding
         *  swap during the unit's scratch draws can't leave us with a
         *  stale RTV/DSV pointer if D3D internally rebinds.
         */
        ID3D11RenderTargetView*   SavedRTV = nullptr;
        ID3D11DepthStencilView*   SavedDSV = nullptr;
        D3D11_VIEWPORT            SavedVP  = {};
        UINT                      SavedVPCount = 0;
    };
}
