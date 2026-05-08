/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Per-frame sprite queue + flush.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#include "always.h"

#include "sprite_queue.h"

#include "debughandler.h"
#include "graphics_device.h"

#include <cstring>


namespace Vinifera::Gfx
{
    SpriteQueue& SpriteQueue::Get()
    {
        static SpriteQueue instance;
        return instance;
    }


    bool SpriteQueue::Initialize(GraphicsDevice& device)
    {
        if (Initialized) {
            return true;
        }
        if (!Batch.Initialize(device, /*max_quads_per_batch*/ 4096)) {
            return false;
        }
        if (!PalEffect.Initialize(device)) {
            Batch.Shutdown();
            return false;
        }
        Commands.reserve(2048);
        Initialized = true;
        return true;
    }


    void SpriteQueue::Shutdown()
    {
        PalEffect.Shutdown();
        Batch.Shutdown();
        Commands.clear();
        Initialized = false;
    }


    void SpriteQueue::Submit(const SpriteDrawCmd& cmd)
    {
        if (!Initialized || cmd.Asset == nullptr || cmd.Palette == nullptr) {
            return;
        }
        Commands.push_back(cmd);
    }


    void SpriteQueue::Clear()
    {
        Commands.clear();
    }


    void SpriteQueue::Flush(GraphicsDevice& device)
    {
        if (!Initialized || Commands.empty()) {
            Commands.clear();
            return;
        }

        const int bb_w = device.Get_Backbuffer_Width();
        const int bb_h = device.Get_Backbuffer_Height();

        device.Bind_Backbuffer();

        /**
         *  Walk the queue in submission order, grouping contiguous commands
         *  that share (Asset, Palette, EffectFlags, Remap). The `Set_Params`
         *  uniform (AtlasSize, Flags) is set once per batch since SpriteBatch
         *  defers all DrawIndexed calls to End() — so the atlas the shader
         *  uses must be constant across a batch.
         */
        const auto state_eq = [](const SpriteDrawCmd& a, const SpriteDrawCmd& b) {
            if (a.Asset != b.Asset) return false;
            if (a.Palette != b.Palette) return false;
            if (a.EffectFlags != b.EffectFlags) return false;
            if (a.UseRemap != b.UseRemap) return false;
            if (a.UseRemap && memcmp(a.RemapTable, b.RemapTable, 16) != 0) return false;
            return true;
        };

        size_t i = 0;
        while (i < Commands.size()) {
            size_t j = i + 1;
            while (j < Commands.size() && state_eq(Commands[i], Commands[j])) {
                ++j;
            }

            const SpriteDrawCmd& head = Commands[i];
            head.Palette->Update_Remap(head.UseRemap ? head.RemapTable : nullptr);

            SpriteEffectParams params = {};
            params.AtlasSize[0] = (float)head.Asset->Get_Atlas().Width();
            params.AtlasSize[1] = (float)head.Asset->Get_Atlas().Height();
            params.Flags = head.EffectFlags;

            Batch.Begin(device, EBlend::Premultiplied, ESampler::PointClamp, &PalEffect, bb_w, bb_h);
            PalEffect.Bind_Palette(device, *head.Palette);
            PalEffect.Set_Params(device, params);

            for (size_t k = i; k < j; ++k) {
                const SpriteDrawCmd& c = Commands[k];
                const ShpFrameInfo* fi = c.Asset->Get_Frame(c.FrameIndex);
                if (fi == nullptr || fi->W <= 0 || fi->H <= 0) {
                    continue;
                }
                const RectF src = { (float)fi->AtlasX, (float)fi->AtlasY, (float)fi->W, (float)fi->H };
                Batch.Draw(&c.Asset->Get_Atlas(), c.Dst, &src, c.VertexTint);
            }

            Batch.End(device);
            i = j;
        }

        /**
         *  Unbind palette/remap SRVs to keep subsequent passes clean.
         */
        ID3D11ShaderResourceView* null_srvs[3] = {};
        device.Get_Context()->PSSetShaderResources(0, 3, null_srvs);

        Commands.clear();
    }
}
