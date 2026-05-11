/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Per-frame voxel composite queue + flush.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#include "always.h"

#include "voxel_composite_queue.h"

#include "debughandler.h"
#include "graphics_device.h"
#include "gpu_surface_target.h"
#include "palette_lut.h"
#include "perf_monitor.h"
#include "states.h"

#include <algorithm>


namespace Vinifera::Gfx
{
    namespace
    {
        /**
         *  Vanilla's `VoxelSurface` / `EightBitSurface` are 160×160. Use 256 to
         *  give headroom for any future bigger raster + power-of-two alignment.
         */
        constexpr int kVoxelAtlasSize = 256;
    }


    VoxelCompositeQueue& VoxelCompositeQueue::Get()
    {
        static VoxelCompositeQueue instance;
        return instance;
    }


    bool VoxelCompositeQueue::Initialize(GraphicsDevice& device)
    {
        if (Initialized) {
            return true;
        }

        /**
         *  Small batch capacity — we Begin/End per cmd since the shared atlas
         *  is overwritten between cmds. 16 quads is more than one cmd ever
         *  needs.
         */
        if (!Batch.Initialize(device, /*max_quads*/ 16)) {
            return false;
        }
        if (!Effect.Initialize(device)) {
            Batch.Shutdown();
            return false;
        }

        if (!ColorAtlas.Initialize(device, kVoxelAtlasSize, kVoxelAtlasSize,
                                   DXGI_FORMAT_R8_UINT, D3D11_USAGE_DEFAULT)) {
            DEBUG_ERROR("VoxelCompositeQueue: ColorAtlas init failed.\n");
            Effect.Shutdown();
            Batch.Shutdown();
            return false;
        }
        if (!ZAtlas.Initialize(device, kVoxelAtlasSize, kVoxelAtlasSize,
                               DXGI_FORMAT_R8_UINT, D3D11_USAGE_DEFAULT)) {
            DEBUG_ERROR("VoxelCompositeQueue: ZAtlas init failed.\n");
            ColorAtlas.Shutdown();
            Effect.Shutdown();
            Batch.Shutdown();
            return false;
        }

        Commands.reserve(256);
        Initialized = true;
        return true;
    }


    void VoxelCompositeQueue::Shutdown()
    {
        ZAtlas.Shutdown();
        ColorAtlas.Shutdown();
        Effect.Shutdown();
        Batch.Shutdown();
        Commands.clear();
        Initialized = false;
    }


    void VoxelCompositeQueue::Submit(const VoxelCompositeCmd& cmd)
    {
        if (!Initialized
            || cmd.ColorData.empty()
            || cmd.Palette == nullptr
            || cmd.SourceW <= 0 || cmd.SourceH <= 0
            || cmd.SourceW > kVoxelAtlasSize || cmd.SourceH > kVoxelAtlasSize) {
            return;
        }
        Commands.push_back(cmd);
        PerfMonitor::Get().Note_Voxel_Composite_Submit();
    }


    void VoxelCompositeQueue::Clear()
    {
        Commands.clear();
    }


    void VoxelCompositeQueue::Flush_Pass(GraphicsDevice& device, RenderPass pass)
    {
        if (!Initialized || Commands.empty()) {
            return;
        }

        std::vector<VoxelCompositeCmd> pass_commands;
        pass_commands.reserve(Commands.size());
        for (const VoxelCompositeCmd& cmd : Commands) {
            if (cmd.Pass == pass) {
                pass_commands.push_back(cmd);
            }
        }
        if (pass_commands.empty()) {
            return;
        }

        static int s_flush_debug_count = 0;
        if (s_flush_debug_count < 20) {
            DEBUG_INFO("Voxel flush pass=%d: %zu cmds (queue total=%zu)\n",
                (int)pass, pass_commands.size(), Commands.size());
            s_flush_debug_count++;
        }

        /**
         *  Bucket by output target so each bucket gets one Bind_Render_Target.
         */
        std::stable_sort(pass_commands.begin(), pass_commands.end(),
            [](const VoxelCompositeCmd& a, const VoxelCompositeCmd& b) {
                return (uint8_t)a.OutputTarget < (uint8_t)b.OutputTarget;
            });

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
                    DEBUG_WARNING("VoxelCompositeQueue: dropping %zu commands with OutputTarget::None.\n",
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

            /**
             *  One Begin/End per cmd: each cmd uploads different pixels to the
             *  shared atlas, so cmds can't share a single batched draw.
             *  Per-cmd cost is one UpdateSubresource + one quad draw — fine
             *  for typical voxel counts (~50–150 per frame). Future optimization
             *  would shelf-pack cmds into a larger atlas and batch.
             */
            for (size_t i = bucket_start; i < bucket_end; ++i) {
                const VoxelCompositeCmd& cmd = pass_commands[i];

                ColorAtlas.Set_Sub_Data(0, 0, cmd.SourceW, cmd.SourceH,
                                        cmd.ColorData.data(), cmd.SourceW);
                const bool has_z = !cmd.ZData.empty();
                if (has_z) {
                    ZAtlas.Set_Sub_Data(0, 0, cmd.SourceW, cmd.SourceH,
                                        cmd.ZData.data(), cmd.SourceW);
                }

                SpriteEffectParams params = {};
                params.AtlasSize[0] = (float)kVoxelAtlasSize;
                params.AtlasSize[1] = (float)kVoxelAtlasSize;
                params.ZShapeAtlasSize[0] = (float)kVoxelAtlasSize;
                params.ZShapeAtlasSize[1] = (float)kVoxelAtlasSize;
                params.ZShapeDepthScale = 1.0f / 16000.0f;
                /**
                 *  Per-vertex flags (DARKEN, USE_ZSHAPE) are now passed via
                 *  SpriteBatch::Draw. Only NO_ALPHA_BUFFER stays in the CB.
                 */
                params.Flags = 0;
                if (is_sidebar) {
                    params.Flags |= SEF_NO_ALPHA_BUFFER;
                }

                EDepthStencil depth_state;
                if (is_sidebar) {
                    depth_state = EDepthStencil::None;
                } else {
                    depth_state = cmd.DisableDepth
                        ? EDepthStencil::None
                        : (cmd.WriteDepth
                            ? EDepthStencil::WriteLessEqual
                            : EDepthStencil::TestLessEqual_NoWrite);
                }

                Batch.Begin(device, EBlend::DualSourceBlend, ESampler::PointClamp, &Effect,
                            target_w, target_h, depth_state);
                Effect.Bind_Palette_Array(device);
                Effect.Set_Params(device, params);

                ID3D11ShaderResourceView* z_srv = has_z ? ZAtlas.Get_SRV() : ColorAtlas.Get_SRV();
                device.Get_Context()->PSSetShaderResources(3, 1, &z_srv);

                ID3D11ShaderResourceView* alpha_srv = device.Get_Alpha_SRV();
                device.Get_Context()->PSSetShaderResources(4, 1, &alpha_srv);

                /**
                 *  Source UV in atlas pixel coords — SpriteBatch normalizes by
                 *  atlas size. Z UV is already normalized.
                 */
                const RectF src = { 0.0f, 0.0f, (float)cmd.SourceW, (float)cmd.SourceH };
                const RectF z_uv = {
                    0.0f, 0.0f,
                    (float)cmd.SourceW / (float)kVoxelAtlasSize,
                    (float)cmd.SourceH / (float)kVoxelAtlasSize
                };

                const uint32_t layer = (cmd.Palette != nullptr && cmd.Palette->Layer() >= 0)
                                     ? (uint32_t)cmd.Palette->Layer() : 0u;
                uint32_t flags = cmd.EffectFlags;
                if (has_z) flags |= SEF_USE_ZSHAPE;

                Batch.Draw(&ColorAtlas, cmd.Dst, &src, cmd.Tint,
                           cmd.DepthBaseline, cmd.DepthBaseline,
                           has_z ? &z_uv : nullptr,
                           cmd.Clip.Is_Valid() ? &cmd.Clip : nullptr,
                           layer, flags);
                Batch.End(device);
                PerfMonitor::Get().Note_Voxel_Composite_Draw_Call();
            }

            bucket_start = bucket_end;
        }

        /**
         *  Unbind palette/remap/z-shape/alpha SRVs to keep subsequent passes clean.
         */
        ID3D11ShaderResourceView* null_srvs[5] = {};
        device.Get_Context()->PSSetShaderResources(0, 5, null_srvs);
    }
}
