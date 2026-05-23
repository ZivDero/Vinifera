/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Lazy-cached pipeline-state presets.
 *
 *          A small set of named state objects covers the vast majority of 2D
 *          rendering paths: alpha blend modes, two sampler filters, scissor
 *          on/off rasterizers, and depth-stencil disabled. The cache lives on
 *          the GraphicsDevice and creates objects on first request.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#pragma once

#include <d3d11.h>


namespace Vinifera::Gfx
{
    class GraphicsDevice;

    enum class EBlend
    {
        Opaque,             // no blend
        AlphaBlend,         // SrcAlpha / InvSrcAlpha (non-premultiplied)
        Premultiplied,      // One     / InvSrcAlpha (matches RmlUi/many sprite paths)
        Additive,           // SrcAlpha / One
        DestMultiplyHalf,   // dest *= 0.5 (vanilla SHAPE_DARKEN; src color irrelevant)
        DualSourceBlend,    // result = src0 + src1 * dest. Shader emits per-pixel
                            // (src0, src1); used by SpriteEffect to express both
                            // Premultiplied and SHAPE_DARKEN without a state switch.
        MinSrcDest,         // result = min(src, dest). Idempotent darken: writing
                            // the same src color multiple times to a pixel produces
                            // the same result. Used by voxel shadow rendering so
                            // overlapping shadow columns at cardinal facings don't
                            // compound-darken into bands.
        Brighten,           // result = dest + dest * src.rgb. Vanilla's multiplicative
                            // brighten (`dst = dst + dst * factor`) used by spotlight,
                            // SHAPE_GLOW, and `Draw_Line_entry_38`. Dark pixels stay
                            // dark; bright pixels saturate. Unlike Additive this does
                            // NOT add a constant gray over dark terrain.

        Count
    };

    enum class ESampler
    {
        LinearClamp,
        PointClamp,

        Count
    };

    enum class ERasterizer
    {
        CullNone,           // no scissor
        CullNoneScissor,    // scissor enabled

        Count
    };

    enum class EDepthStencil
    {
        None,                   // depth & stencil off (default for 2D)
        WriteLessEqual,         // depth-test LessEqual + depth-write enabled (terrain tiles)
        TestLessEqual_NoWrite,  // depth-test LessEqual, depth-write disabled (sprites)
        WriteLess,              // depth-test Less + depth-write enabled. Used by voxel
                                // shadow rendering so multiple shadow voxels projecting to
                                // the same pixel only land once — the first write sets the
                                // depth, subsequent equal-depth writes are rejected.
        DarkenDedup,            // depth-test LessEqual + no depth write + stencil dedup
                                // (StencilFunc EQUAL with ref=0, StencilPassOp INCR_SAT).
                                // First darken at a pixel passes (stencil 0 == ref 0), op
                                // increments stencil to 1; subsequent darkens at the same
                                // pixel fail (1 != 0) and are discarded. Used by sprite
                                // SHAPE_DARKEN draws so overlapping shadows (cliff +
                                // bridge, infantry shadow + cliff, etc.) darken once.

        Count
    };

    /**
     *  Cache of preset state objects, owned by GraphicsDevice. State objects
     *  are created lazily on first Get_*; subsequent calls return the cached
     *  instance. The device releases everything on shutdown.
     */
    class StateCache
    {
    public:
        StateCache() = default;
        ~StateCache();

        StateCache(const StateCache&) = delete;
        StateCache& operator=(const StateCache&) = delete;

        void Initialize(ID3D11Device* device);
        void Shutdown();

        ID3D11BlendState*        Get(EBlend blend);
        ID3D11SamplerState*      Get(ESampler sampler);
        ID3D11RasterizerState*   Get(ERasterizer rasterizer);
        ID3D11DepthStencilState* Get(EDepthStencil depth_stencil);

    private:
        ID3D11Device*            Device = nullptr;

        ID3D11BlendState*        Blends[(size_t)EBlend::Count] = {};
        ID3D11SamplerState*      Samplers[(size_t)ESampler::Count] = {};
        ID3D11RasterizerState*   Rasterizers[(size_t)ERasterizer::Count] = {};
        ID3D11DepthStencilState* DepthStencils[(size_t)EDepthStencil::Count] = {};
    };
}
