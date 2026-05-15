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
     *  Fixed scratch size — large enough for any TS unit (Mammoth Mk II is
     *  ~80x80 visible; predator/walker units fit comfortably). Doubled from
     *  vanilla's 160x160 EightBitSurface to give effects headroom.
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
     *  Effect that samples the scratch RT as a plain RGBA texture and pre-
     *  multiplies by a unit-level alpha for the final composite blend.
     *  Bind layout:
     *    t0 — scratch RT SRV
     *    s0 — point-clamp sampler
     *    b0 — SpriteCB (ProjMtx from SpriteBatch)
     *    b1 — UnitCompositeCB (alpha + unused)
     */
    class UnitCompositeEffect : public Effect
    {
    public:
        struct Params
        {
            float Alpha;
            float _Pad[3];
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
         *  Bind the scratch RTV+DSV with a 256x256 viewport, clear color to
         *  transparent and depth to 1.0. Saves the active RT/DSV bindings so
         *  `End_Unit_Composite` can restore them.
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

        RenderTarget2D*           ScratchRT  = nullptr;
        ID3D11Texture2D*          DepthTex   = nullptr;
        ID3D11DepthStencilView*   DepthDSV   = nullptr;

        SpriteBatch               CompositeBatch;
        UnitCompositeEffect       CompositeFx;

        bool                      Initialized = false;

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
