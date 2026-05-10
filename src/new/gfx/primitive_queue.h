/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Per-frame solid-color tactical primitive queue.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#pragma once

#include <vector>

#include "dynamic_buffer.h"
#include "effect.h"
#include "render_pass.h"
#include "sprite_batch.h"
#include "states.h"


namespace Vinifera::Gfx
{
    class GraphicsDevice;


    enum class PrimitiveKind : unsigned char
    {
        SolidRect,
        Line,
    };


    struct PrimitiveDrawCmd
    {
        PrimitiveKind Kind = PrimitiveKind::SolidRect;
        RenderPass    Pass = RenderPass::UiOverlay;
        EBlend        Blend = EBlend::Opaque;
        RectF         Rect = {};
        float         X0 = 0.0f;
        float         Y0 = 0.0f;
        float         X1 = 0.0f;
        float         Y1 = 0.0f;
        float         Thickness = 1.0f;
        float         Color[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    };


    class PrimitiveQueue
    {
    public:
        static PrimitiveQueue& Get();

        bool Initialize(GraphicsDevice& device);
        void Shutdown();

        bool Is_Initialized() const { return Initialized; }

        void Submit(const PrimitiveDrawCmd& cmd);
        void Flush_Pass(GraphicsDevice& device, RenderPass pass);
        void Clear();

    private:
        struct PrimitiveVertex
        {
            float Pos[2];
            float Color[4];
        };

        struct PrimitiveCB
        {
            float ProjMtx[16];
        };

        PrimitiveQueue() = default;

        bool Create_Effect(GraphicsDevice& device);
        void Emit_Rect(std::vector<PrimitiveVertex>& vertices, const RectF& rect, const float color[4]);
        void Emit_Line(std::vector<PrimitiveVertex>& vertices, const PrimitiveDrawCmd& cmd);
        void Draw_Group(GraphicsDevice& device, const std::vector<PrimitiveDrawCmd>& commands, size_t begin, size_t end);

        DynamicVertexBuffer<PrimitiveVertex> VertexBuffer;
        Effect                               PrimitiveEffect;
        std::vector<PrimitiveDrawCmd>        Commands;
        bool                                 Initialized = false;
    };
}
