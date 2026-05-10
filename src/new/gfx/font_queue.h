/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Per-frame glyph queue for the GPU WWFont path.
 *
 *          The `WWFontClass::Print` proxy decomposes each Print() call into
 *          one `FontDrawCmd` per visible glyph and submits them here. Flush
 *          buckets by `OutputTarget` (Scene / Sidebar), groups within each
 *          bucket by (Asset, Palette, Remap), and emits one quad per glyph
 *          via `SpriteBatch`.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#pragma once

#include <cstdint>
#include <vector>

#include "font_effect.h"
#include "gpu_surface_target.h"
#include "render_pass.h"
#include "sprite_batch.h"


namespace Vinifera::Gfx
{
    class FontAsset;
    class GraphicsDevice;
    class PaletteLUT;


    struct FontDrawCmd
    {
        FontAsset*       Asset = nullptr;
        PaletteLUT*      Palette = nullptr;
        uint8_t          Glyph = 0;
        RectF            Dst = {};
        RectF            Clip = {};
        uint8_t          RemapTable[16] = {};
        RenderPass       Pass = RenderPass::UiOverlay;
        GpuRenderTarget  OutputTarget = GpuRenderTarget::Scene;
    };


    class FontQueue
    {
    public:
        static FontQueue& Get();

        bool Initialize(GraphicsDevice& device);
        void Shutdown();

        bool Is_Initialized() const { return Initialized; }

        void Submit(const FontDrawCmd& cmd);
        void Flush_Pass(GraphicsDevice& device, RenderPass pass);
        void Clear();

    private:
        FontQueue() = default;

        SpriteBatch                Batch;
        FontEffect                 Effect;
        std::vector<FontDrawCmd>   Commands;
        bool                       Initialized = false;
    };
}
