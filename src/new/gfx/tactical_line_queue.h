/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Per-frame queue for depth/alpha-aware tactical lines.
 *
 *          Backing for `GpuSurface::Draw_Z_Line` / `Brighten_Line` /
 *          `Draw_Gradient_Z_Line` / `Draw_Dashed_Alpha_Line` /
 *          `Draw_Alpha_Line`. Each cmd is one line segment with a `TacticalLineFlag`
 *          bitmask that controls z-test, z-write, alpha modulation, alpha
 *          masking, and optional color gradient. The shader (in
 *          `TacticalLineEffect`) samples `SceneRT.DepthSRV` and `AlphaSRV`
 *          per fragment.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#pragma once

#include <cstdint>
#include <vector>

#include "dynamic_buffer.h"
#include "gpu_surface_target.h"
#include "render_pass.h"
#include "states.h"
#include "tactical_line_effect.h"


namespace Vinifera::Gfx
{
    class GraphicsDevice;


    struct TacticalLineCmd
    {
        float           X0 = 0.0f;
        float           Y0 = 0.0f;
        float           X1 = 0.0f;
        float           Y1 = 0.0f;
        float           Thickness = 1.0f;
        float           ZStart = 0.0f;
        float           ZEnd = 0.0f;
        float           ColorStart[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
        float           ColorEnd[4]   = { 1.0f, 1.0f, 1.0f, 1.0f };
        uint32_t        Flags = TLF_NONE;
        EBlend          Blend = EBlend::Opaque;
        EDepthStencil   Depth = EDepthStencil::None;
        RenderPass      Pass = RenderPass::UiOverlay;
        GpuRenderTarget OutputTarget = GpuRenderTarget::Scene;
    };


    class TacticalLineQueue
    {
    public:
        static TacticalLineQueue& Get();

        bool Initialize(GraphicsDevice& device);
        void Shutdown();

        bool Is_Initialized() const { return Initialized; }

        void Submit(const TacticalLineCmd& cmd);
        void Flush_Pass(GraphicsDevice& device, RenderPass pass);
        void Clear();

    private:
        struct LineVertex { float Pos[2]; float T; };

        TacticalLineQueue() = default;

        void Draw_Cmd(GraphicsDevice& device, const TacticalLineCmd& cmd, int target_w, int target_h);

        DynamicVertexBuffer<LineVertex>    VertexBuffer;
        TacticalLineEffect                 Effect;
        std::vector<TacticalLineCmd>       Commands;
        bool                               Initialized = false;
    };
}
