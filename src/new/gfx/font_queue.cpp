/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Per-frame glyph queue for the GPU WWFont path.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#include "always.h"

#include "font_queue.h"

#include "debughandler.h"
#include "font_asset.h"
#include "graphics_device.h"
#include "palette_lut.h"

#include <algorithm>
#include <cstring>


namespace Vinifera::Gfx
{
    FontQueue& FontQueue::Get()
    {
        static FontQueue instance;
        return instance;
    }


    bool FontQueue::Initialize(GraphicsDevice& device)
    {
        if (Initialized) {
            return true;
        }
        if (!Batch.Initialize(device, /*max_quads_per_batch*/ 4096)) {
            return false;
        }
        if (!Effect.Initialize(device)) {
            Batch.Shutdown();
            return false;
        }
        Commands.reserve(1024);
        Initialized = true;
        return true;
    }


    void FontQueue::Shutdown()
    {
        Effect.Shutdown();
        Batch.Shutdown();
        Commands.clear();
        Initialized = false;
    }


    void FontQueue::Submit(const FontDrawCmd& cmd)
    {
        if (!Initialized || cmd.Asset == nullptr || cmd.Palette == nullptr) {
            return;
        }
        Commands.push_back(cmd);
    }


    void FontQueue::Clear()
    {
        Commands.clear();
    }


    void FontQueue::Flush_Pass(GraphicsDevice& device, RenderPass pass)
    {
        if (!Initialized || Commands.empty()) {
            return;
        }

        std::vector<FontDrawCmd> pass_commands;
        pass_commands.reserve(Commands.size());
        for (const FontDrawCmd& cmd : Commands) {
            if (cmd.Pass == pass) {
                pass_commands.push_back(cmd);
            }
        }
        if (pass_commands.empty()) {
            return;
        }

        /**
         *  Bucket by output target. Stable-sort preserves submission order
         *  within a bucket, so multi-color text within the same Print call
         *  keeps its layout.
         */
        std::stable_sort(pass_commands.begin(), pass_commands.end(),
            [](const FontDrawCmd& a, const FontDrawCmd& b) {
                return (uint8_t)a.OutputTarget < (uint8_t)b.OutputTarget;
            });

        const auto state_eq = [](const FontDrawCmd& a, const FontDrawCmd& b) {
            if (a.Asset != b.Asset) return false;
            if (a.Palette != b.Palette) return false;
            if (memcmp(a.RemapTable, b.RemapTable, 16) != 0) return false;
            return true;
        };

        size_t bucket_start = 0;
        while (bucket_start < pass_commands.size()) {
            const GpuRenderTarget bucket_target = pass_commands[bucket_start].OutputTarget;
            size_t bucket_end = bucket_start + 1;
            while (bucket_end < pass_commands.size()
                && pass_commands[bucket_end].OutputTarget == bucket_target) {
                ++bucket_end;
            }

            if (bucket_target == GpuRenderTarget::None) {
                static bool warned = false;
                if (!warned) {
                    DEBUG_WARNING("FontQueue: dropping %zu commands with OutputTarget::None.\n",
                        bucket_end - bucket_start);
                    warned = true;
                }
                bucket_start = bucket_end;
                continue;
            }

            Bind_Render_Target(device, bucket_target);

            const bool is_sidebar = (bucket_target == GpuRenderTarget::Sidebar);
            const int target_w = is_sidebar ? device.Get_Sidebar_Target_Width()  : device.Get_Backbuffer_Width();
            const int target_h = is_sidebar ? device.Get_Sidebar_Target_Height() : device.Get_Backbuffer_Height();
            const EDepthStencil depth_state = EDepthStencil::None;   // text is UI overlay; no depth

            size_t i = bucket_start;
            while (i < bucket_end) {
                size_t j = i + 1;
                while (j < bucket_end && state_eq(pass_commands[i], pass_commands[j])) {
                    ++j;
                }

                const FontDrawCmd& head = pass_commands[i];

                FontEffectParams params = {};
                params.AtlasSize[0] = (float)head.Asset->Get_Atlas().Width();
                params.AtlasSize[1] = (float)head.Asset->Get_Atlas().Height();
                for (int k = 0; k < 16; ++k) {
                    params.Remap[k] = head.RemapTable[k];
                }

                Batch.Begin(device, EBlend::Premultiplied, ESampler::PointClamp,
                            &Effect, target_w, target_h, depth_state);
                Effect.Bind_Palette(device, *head.Palette);
                Effect.Set_Params(device, params);

                const float tint[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

                for (size_t k = i; k < j; ++k) {
                    const FontDrawCmd& c = pass_commands[k];
                    const FontGlyphInfo* gi = c.Asset->Get_Glyph(c.Glyph);
                    if (gi == nullptr || gi->W <= 0 || gi->H <= 0) {
                        continue;
                    }
                    const RectF src = { (float)gi->AtlasX, (float)gi->AtlasY,
                                        (float)gi->W,      (float)gi->H };
                    Batch.Draw(&c.Asset->Get_Atlas(), c.Dst, &src, tint, 0.0f, 0.0f,
                               nullptr,
                               c.Clip.Is_Valid() ? &c.Clip : nullptr);
                }

                Batch.End(device);
                i = j;
            }

            bucket_start = bucket_end;
        }

        /**
         *  Unbind palette SRV so subsequent passes start clean.
         */
        ID3D11ShaderResourceView* null_srvs[2] = {};
        device.Get_Context()->PSSetShaderResources(0, 2, null_srvs);
    }
}
