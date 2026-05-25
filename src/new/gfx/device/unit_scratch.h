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
     *  Backing-storage SSAA factor — the SSAA color + depth textures are
     *  allocated at `(kUnitScratchWidth × kUnitScratchHeight)` multiplied
     *  by this. There is a SECOND, no-SSAA RT pair at exactly logical
     *  size for the dual-pass composite blend (see UnitScratch).
     *
     *  2 is the right default — 4× would quadruple PS cost across the
     *  whole unit footprint for a barely-perceptible improvement at
     *  256² logical.
     */
    constexpr int kUnitScratchMaxSSAA       = 2;
    constexpr int kUnitScratchSSAAWidth     = kUnitScratchWidth  * kUnitScratchMaxSSAA;
    constexpr int kUnitScratchSSAAHeight    = kUnitScratchHeight * kUnitScratchMaxSSAA;


    /**
     *  Composite resolve effect. Dual-pass blend: averages a 4-tap SSAA
     *  resolve (from t1) and a 1-tap point sample (from t0); when only
     *  the NoSSAA pass was rendered (PassCount == 1, SmoothVoxels=off)
     *  the PS skips t1 entirely and emits the t0 sample as-is. See
     *  unit_composite.hlsl.
     *
     *  Bind layout:
     *    t0 — NoSSAA scratch SRV (256², one source texel per dst pixel)
     *    t1 — SSAA   scratch SRV (512², 2×2 source block per dst pixel)
     *    s0 — point-clamp sampler (unused; PS uses Load())
     *    b0 — SpriteCB (ProjMtx from SpriteBatch)
     *    b1 — UnitCompositeCB (alpha + SSAA factor + pass count)
     */
    class UnitCompositeEffect : public Effect
    {
    public:
        struct Params
        {
            float Alpha;
            int   Ssaa;        // SSAA factor used for the t1 RT (currently always kUnitScratchMaxSSAA when sampled)
            int   PassCount;   // 1 = NoSSAA only; 2 = blend NoSSAA + SSAA
            float _Pad;
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
         *  Active SSAA factor for the currently-bound pass — read by
         *  `VoxelQueue::Issue_Cmd_To_Scratch` and
         *  `SpriteQueue::Render_Sprite_To_Scratch_Immediate` to scale
         *  their screen-space rects and pick render target dims. Set by
         *  `Begin_Unit_Pass` (1 for the NoSSAA pass, kUnitScratchMaxSSAA
         *  for the SSAA pass) and reset by `End_Unit_Composite`.
         */
        int Get_Active_SSAA()   const { return CurrentSsaa; }
        int Get_Active_Width()  const { return kUnitScratchWidth  * CurrentSsaa; }
        int Get_Active_Height() const { return kUnitScratchHeight * CurrentSsaa; }

        /**
         *  How many passes to render this unit through — sampled from
         *  `RuleExtension->IsSmoothVoxels`. SmoothVoxels=off → 1 (NoSSAA
         *  pass only, vanilla look). SmoothVoxels=on → 2 (NoSSAA + SSAA
         *  passes, averaged at composite for a softened blend). Callers
         *  loop `for (int p = 0; p < count; ++p) Begin_Unit_Pass(...,p)`
         *  and re-render every section per pass.
         */
        int Get_Pass_Count() const;

        /**
         *  Begin pass `pass` of this unit (0 ≤ pass < Get_Pass_Count()).
         *  Pass 0 is always the NoSSAA pass (256² RT, CurrentSsaa = 1,
         *  POINTLIST voxels via VEF_SPLAT mask in the queue helper).
         *  Pass 1 is the SSAA pass (512² RT, CurrentSsaa =
         *  kUnitScratchMaxSSAA, splatted voxels). Pass 0 also saves the
         *  caller's scene RT/DSV/viewport so End_Unit_Composite can
         *  restore them; later passes just rebind the scratch state.
         */
        bool Begin_Unit_Pass(GraphicsDevice& device, int pass);

        /**
         *  Clear the active pass's scratch depth buffer to 1.0 without
         *  disturbing the scratch color RT. Used between captured
         *  records so each record's voxel/SHP draws start from a fresh
         *  depth — gives strict painter's-order layering between
         *  records (body, voxel barrel, turret) while preserving per-
         *  voxel depth resolution within a single record's section.
         *  Operates on the currently-bound pass's DSV.
         */
        void Clear_Depth(GraphicsDevice& device);

        /**
         *  Restore the scene RTV/DSV/viewport, then composite the
         *  scratch contents as a single quad. When 2 passes were
         *  rendered the composite PS averages the NoSSAA (crisp) and
         *  SSAA (smooth) results; when only 1 pass was rendered (NoSSAA
         *  only) the PS pass-throughs it. `scene_origin` is where the
         *  scratch's (0, 0) corner lands in scene-RT pixels — for unit-
         *  local rendering centered at the unit's drawpoint, pass
         *  `drawpoint - scratch_origin`. `alpha` is the visual-character
         *  translucency [0..1]. `scene_depth` is the depth emitted by
         *  every pixel of the quad.
         */
        void End_Unit_Composite(GraphicsDevice& device,
                                Point2D scene_origin,
                                float alpha,
                                float scene_depth);

    private:
        UnitScratch() = default;

        bool Ensure_Targets(GraphicsDevice& device);
        void Release_Targets();

        /**
         *  Two scratch color targets, one per dual-pass component:
         *    ScratchNoSSAA: 256² — POINTLIST voxels at 1× scale, crisp
         *                   single-tap source for the composite blend.
         *                   Always rendered (used for both single-pass
         *                   SmoothVoxels=off and dual-pass on).
         *    ScratchSSAA:   512² — splatted voxels at 2× scale, smooth
         *                   4-tap source. Rendered only in dual-pass
         *                   (Get_Pass_Count() == 2).
         *  Both RTV+SRV via RenderTarget2D, single-sample so the
         *  composite PS can sample them directly without a resolve.
         */
        RenderTarget2D*           ScratchNoSSAA  = nullptr;
        RenderTarget2D*           ScratchSSAA    = nullptr;

        /**
         *  Per-pass depth targets, sized to match the respective color
         *  RT. Single-sample D32_FLOAT, BIND_DEPTH_STENCIL only — never
         *  sampled or resolved; consumed only during the per-pass
         *  scratch render.
         */
        ID3D11Texture2D*          DepthTexNoSSAA = nullptr;
        ID3D11DepthStencilView*   DepthDSVNoSSAA = nullptr;
        ID3D11Texture2D*          DepthTexSSAA   = nullptr;
        ID3D11DepthStencilView*   DepthDSVSSAA   = nullptr;

        SpriteBatch               CompositeBatch;
        UnitCompositeEffect       CompositeFx;

        bool                      Initialized = false;

        /**
         *  Active SSAA factor for the currently-bound pass. Set by
         *  `Begin_Unit_Pass` (1 for the NoSSAA pass, kUnitScratchMaxSSAA
         *  for the SSAA pass). Reset to the max default by
         *  `End_Unit_Composite` so a stray query between units returns
         *  something sane.
         */
        int                       CurrentSsaa = kUnitScratchMaxSSAA;

        /**
         *  Pass-state tracking for the current unit:
         *    ActivePassCount  — sampled from Get_Pass_Count() at the
         *                       Begin_Unit_Pass(0) call and held until
         *                       End_Unit_Composite, so a live rule edit
         *                       between passes can't desync the loop.
         *    ActivePassIndex  — index passed to the most recent
         *                       Begin_Unit_Pass; consumed by Clear_Depth
         *                       to pick which DSV to clear. -1 between
         *                       units.
         */
        int                       ActivePassCount = 0;
        int                       ActivePassIndex = -1;

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
