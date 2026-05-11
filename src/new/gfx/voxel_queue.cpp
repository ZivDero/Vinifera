/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Per-frame voxel draw queue + flush.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#include "always.h"

#include "voxel_queue.h"

#include "debughandler.h"
#include "graphics_device.h"
#include "gpu_surface_target.h"
#include "palette_lut.h"
#include "perf_monitor.h"
#include "voxel_asset.h"

#include <algorithm>
#include <cstring>


namespace Vinifera::Gfx
{
    VoxelQueue& VoxelQueue::Get()
    {
        static VoxelQueue instance;
        return instance;
    }


    bool VoxelQueue::Initialize(GraphicsDevice& device)
    {
        if (Initialized) {
            return true;
        }
        if (!EffectInstance.Initialize(device)) {
            return false;
        }
        if (!LightRemapInstance.Initialize(device)) {
            EffectInstance.Shutdown();
            return false;
        }
        Commands.reserve(256);
        Initialized = true;
        return true;
    }


    void VoxelQueue::Shutdown()
    {
        LightRemapInstance.Shutdown();
        EffectInstance.Shutdown();
        Commands.clear();
        Initialized = false;
    }


    void VoxelQueue::Submit(const VoxelDrawCmd& cmd)
    {
        if (!Initialized) {
            return;
        }
        if (cmd.Mesh == nullptr) {
            return;
        }
        const uint32_t count = cmd.IsShadow ? cmd.Mesh->ShadowVertexCount : cmd.Mesh->VertexCount;
        if (count == 0) {
            return;
        }
        Commands.push_back(cmd);
        PerfMonitor::Get().Note_Voxel_Composite_Submit();
    }


    void VoxelQueue::Clear()
    {
        Commands.clear();
    }


    void VoxelQueue::Issue_Cmd(GraphicsDevice& device, const VoxelDrawCmd& cmd,
                               int target_w, int target_h, bool is_sidebar)
    {
        ID3D11DeviceContext* ctx = device.Get_Context();
        if (ctx == nullptr) {
            return;
        }

        ID3D11Buffer* vb = cmd.IsShadow ? cmd.Mesh->ShadowVertexBuffer : cmd.Mesh->VertexBuffer;
        const uint32_t vert_count = cmd.IsShadow ? cmd.Mesh->ShadowVertexCount : cmd.Mesh->VertexCount;
        if (vb == nullptr || vert_count == 0) {
            return;
        }

        /**
         *  Project screen-pixel coords to NDC. Mirrors
         *  TacticalLineQueue::Draw_Cmd's matrix build.
         */
        const float L = 0.0f;
        const float R = (float)target_w;
        const float T = 0.0f;
        const float B = (float)target_h;
        struct ProjCB { float Mtx[16]; } pcb = {};
        pcb.Mtx[0]  = 2.0f / (R - L);
        pcb.Mtx[5]  = 2.0f / (T - B);
        pcb.Mtx[10] = 1.0f;
        pcb.Mtx[12] = (R + L) / (L - R);
        pcb.Mtx[13] = (T + B) / (B - T);
        pcb.Mtx[15] = 1.0f;
        EffectInstance.Set_Constants(device, &pcb);

        EffectInstance.Set_Params(device, cmd.Params);

        D3D11_VIEWPORT vp = {};
        vp.Width    = (float)target_w;
        vp.Height   = (float)target_h;
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;
        ctx->RSSetViewports(1, &vp);
        ctx->RSSetState(device.States().Get(ERasterizer::CullNone));

        // Object voxels: WriteLessEqual with voxel_z anchored at 0.5 baseline
        // (well in front of terrain's ~0.97) gives within-unit painter's via
        // depth buffer while bypassing voxel-vs-terrain competition.
        //
        // Shadow voxels: WriteLess at a fixed depth (0.51, shader-set). The
        // strict LESS comparison rejects equal-depth writes, so when multiple
        // shadow columns project to the same pixel (cardinal facings) only
        // the first write lands — preventing DestMultiplyHalf from compound-
        // darkening into pitch-black. Subsequent object voxels at depth
        // ~0.5 still pass LessEqual against the shadow's 0.51 and overdraw.
        //
        // Sidebar: no depth attachment regardless.
        EDepthStencil depth_state;
        if (is_sidebar) {
            depth_state = EDepthStencil::None;
        } else if (cmd.IsShadow) {
            depth_state = EDepthStencil::WriteLess;
        } else {
            depth_state = EDepthStencil::WriteLessEqual;
        }
        ctx->OMSetDepthStencilState(device.States().Get(depth_state), 0);

        // Shadows: DestMultiplyHalf with blend_factor=(0.5, 0.5, 0.5, 1) so
        // dest pixels get halved. The WriteLess depth state above ensures
        // each pixel is only darkened once even if many shadow columns
        // project to it. Objects: Premultiplied (standard alpha blend).
        const float shadow_factor[4] = { 0.5f, 0.5f, 0.5f, 1.0f };
        const float object_factor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        const EBlend blend = cmd.IsShadow ? EBlend::DestMultiplyHalf : EBlend::Premultiplied;
        const float* factor = cmd.IsShadow ? shadow_factor : object_factor;
        ctx->OMSetBlendState(device.States().Get(blend), factor, 0xFFFFFFFFu);

        EffectInstance.Apply(device);
        EffectInstance.Bind_Normals(device);
        if (cmd.Palette != nullptr) {
            EffectInstance.Bind_Palette(device, *cmd.Palette);
        }
        EffectInstance.Bind_Light_Remap(device, LightRemapInstance.Get_Texture());

        UINT stride = 8;       // sizeof(VoxelVertex)
        UINT offset = 0;
        ctx->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
        ctx->Draw(vert_count, 0);
        PerfMonitor::Get().Note_Voxel_Composite_Draw_Call();
    }


    void VoxelQueue::Flush_Pass(GraphicsDevice& device, RenderPass pass)
    {
        if (!Initialized || Commands.empty()) {
            return;
        }

        /**
         *  Filter to a working list keyed on the requested pass. Stable
         *  sort by (OutputTarget, IsShadow, Mesh) so each bucket binds a
         *  target once and groups same-mesh draws.
         */
        std::vector<const VoxelDrawCmd*> work;
        work.reserve(Commands.size());
        for (const VoxelDrawCmd& cmd : Commands) {
            if (cmd.Pass == pass) {
                work.push_back(&cmd);
            }
        }
        if (work.empty()) {
            return;
        }

        /**
         *  First-flush upload of the game-wide VPL light-remap. No-op
         *  after the first successful upload; `voxels.vpl` is static
         *  across the run.
         */
        LightRemapInstance.Ensure_Uploaded();

        std::stable_sort(work.begin(), work.end(),
            [](const VoxelDrawCmd* a, const VoxelDrawCmd* b) {
                if (a->OutputTarget != b->OutputTarget) {
                    return (uint8_t)a->OutputTarget < (uint8_t)b->OutputTarget;
                }
                if (a->IsShadow != b->IsShadow) {
                    // Shadows render FIRST so unit bodies overdraw them.
                    return a->IsShadow && !b->IsShadow;
                }
                return a->Mesh < b->Mesh;
            });

        size_t bucket_start = 0;
        while (bucket_start < work.size()) {
            const GpuRenderTarget bucket_target = work[bucket_start]->OutputTarget;
            size_t bucket_end = bucket_start + 1;
            while (bucket_end < work.size()
                && work[bucket_end]->OutputTarget == bucket_target) {
                ++bucket_end;
            }

            if (bucket_target == GpuRenderTarget::None) {
                bucket_start = bucket_end;
                continue;
            }

            Bind_Render_Target(device, bucket_target);

            const bool is_sidebar = (bucket_target == GpuRenderTarget::Sidebar);
            const int target_w = is_sidebar ? device.Get_Sidebar_Target_Width()  : device.Get_Logical_Width();
            const int target_h = is_sidebar ? device.Get_Sidebar_Target_Height() : device.Get_Logical_Height();

            for (size_t i = bucket_start; i < bucket_end; ++i) {
                Issue_Cmd(device, *work[i], target_w, target_h, is_sidebar);
            }

            bucket_start = bucket_end;
        }

        /**
         *  Unbind PS SRVs so subsequent passes start clean.
         */
        ID3D11ShaderResourceView* null_srvs[3] = {};
        device.Get_Context()->PSSetShaderResources(0, 3, null_srvs);
    }
}
