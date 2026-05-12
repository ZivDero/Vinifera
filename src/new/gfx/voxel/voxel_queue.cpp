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
#include "scene_copy.h"
#include "states.h"
#include "unit_scratch.h"
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
        if (!DistortionEffectInstance.Initialize(device)) {
            EffectInstance.Shutdown();
            return false;
        }
        if (!LightRemapInstance.Initialize(device)) {
            DistortionEffectInstance.Shutdown();
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
        DistortionEffectInstance.Shutdown();
        EffectInstance.Shutdown();
        Commands.clear();
        Initialized = false;
    }


    bool VoxelQueue::Has_Predator_Commands(RenderPass pass) const
    {
        for (const VoxelDrawCmd& cmd : Commands) {
            if (cmd.Pass == pass && cmd.IsPredator) {
                return true;
            }
        }
        return false;
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
        UnitGroups.clear();
    }


    int VoxelQueue::Allocate_Unit_Group(const VoxelUnitGroup& group)
    {
        UnitGroups.push_back(group);
        return (int)UnitGroups.size() - 1;
    }


    void VoxelQueue::Render_Cmd_Immediate(GraphicsDevice& device, const VoxelDrawCmd& cmd,
                                          int target_w, int target_h, bool is_sidebar)
    {
        if (!Initialized) return;
        /**
         *  First-flush upload of the game-wide VPL light-remap. Idempotent.
         *  The deferred path also calls this in Flush_Pass; doing it here
         *  covers the case where Composite_Replay fires before the regular
         *  flush has run for this frame.
         */
        LightRemapInstance.Ensure_Uploaded();
        Issue_Cmd(device, cmd, target_w, target_h, is_sidebar);
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


    void VoxelQueue::Issue_Predator_Cmd(GraphicsDevice& device, const VoxelDrawCmd& cmd,
                                         int target_w, int target_h)
    {
        ID3D11DeviceContext* ctx = device.Get_Context();
        if (ctx == nullptr) {
            return;
        }

        ID3D11Buffer* vb = cmd.Mesh->VertexBuffer;
        const uint32_t vert_count = cmd.Mesh->VertexCount;
        if (vb == nullptr || vert_count == 0) {
            return;
        }

        /**
         *  Same projection-matrix setup as `Issue_Cmd`. Distortion CB is
         *  separate from the normal voxel CB so we can keep both warm.
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
        DistortionEffectInstance.Set_Constants(device, &pcb);

        /**
         *  Patch SceneCopy size into Params.Predator before upload. The
         *  caller already stamped warp + blend; we own the size half so
         *  the predator path doesn't need to know about SceneCopy.
         */
        VoxelEffectParams params = cmd.Params;
        params.Predator[2] = (float)SceneCopy::Get().Get_Width();
        params.Predator[3] = (float)SceneCopy::Get().Get_Height();
        DistortionEffectInstance.Set_Params(device, params);

        D3D11_VIEWPORT vp = {};
        vp.Width    = (float)target_w;
        vp.Height   = (float)target_h;
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;
        ctx->RSSetViewports(1, &vp);
        ctx->RSSetState(device.States().Get(ERasterizer::CullNone));

        /**
         *  TestLessEqual_NoWrite — predator voxels are occluded by closer
         *  geometry (terrain/buildings already in the depth buffer) but
         *  don't disturb depth for later passes. Matches the SHP predator
         *  distortion path.
         */
        ctx->OMSetDepthStencilState(device.States().Get(EDepthStencil::TestLessEqual_NoWrite), 0);

        /**
         *  Opaque blend — the predator PS does its own lerp with the scene
         *  copy, so the output is the final composited color. No source-
         *  alpha blending against the framebuffer.
         */
        const float factor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        ctx->OMSetBlendState(device.States().Get(EBlend::Opaque), factor, 0xFFFFFFFFu);

        DistortionEffectInstance.Apply(device);
        DistortionEffectInstance.Bind_Normals(device, EffectInstance.Get_Normals_SRV());
        if (cmd.Palette != nullptr) {
            DistortionEffectInstance.Bind_Palette(device, *cmd.Palette);
        }
        DistortionEffectInstance.Bind_Light_Remap(device, LightRemapInstance.Get_Texture());
        DistortionEffectInstance.Bind_Scene_Copy(device, SceneCopy::Get().Get_SRV());

        UINT stride = 8;
        UINT offset = 0;
        ctx->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
        ctx->Draw(vert_count, 0);
        PerfMonitor::Get().Note_Voxel_Composite_Draw_Call();
    }


    void VoxelQueue::Flush_Composite_Group(GraphicsDevice& device,
                                           const VoxelDrawCmd* const* cmds,
                                           size_t count,
                                           const VoxelUnitGroup& group)
    {
        if (count == 0 || !UnitScratch::Get().Is_Initialized()) {
            return;
        }
        if (!UnitScratch::Get().Begin_Unit(device)) {
            return;
        }

        /**
         *  Render each section into the scratch using the standard voxel
         *  path. `Issue_Cmd` already does the right thing — same shader,
         *  same depth math — and the scratch's viewport (set by Begin_Unit)
         *  drives the projection matrix to 256x256 local coords because we
         *  pass target_w/h = scratch size. Voxels with WriteLessEqual depth
         *  test against the scratch's depth buffer so within-unit front-
         *  most wins. Output to scratch is premultiplied opaque (Tint.a
         *  forced to 1.0 at submit time for composite cmds).
         */
        for (size_t k = 0; k < count; ++k) {
            Issue_Cmd(device, *cmds[k], kUnitScratchWidth, kUnitScratchHeight, /*is_sidebar*/ false);
        }

        const Point2D scene_origin {
            group.Drawpoint.X - kUnitScratchOrigin.X,
            group.Drawpoint.Y - kUnitScratchOrigin.Y
        };
        UnitScratch::Get().End_Unit_Composite(device, scene_origin, group.Alpha, group.SceneDepth);
    }


    void VoxelQueue::Flush_Pass(GraphicsDevice& device, RenderPass pass)
    {
        if (!Initialized || Commands.empty()) {
            return;
        }

        /**
         *  Predator commands flush only in PostEffects (after the main scene
         *  has rendered into the Scene RT and SceneCopy can sample it).
         *  Non-predator commands flush in their requested pass (typically
         *  ObjectLayer). Split cmds into:
         *    - non-composite (UnitGroupID == -1): existing fast batched path
         *    - composite    (UnitGroupID >= 0):    grouped scratch-RT path
         */
        const bool predator_pass = (pass == RenderPass::PostEffects);
        std::vector<const VoxelDrawCmd*> non_composite;
        std::vector<const VoxelDrawCmd*> composite;
        non_composite.reserve(Commands.size());
        for (const VoxelDrawCmd& cmd : Commands) {
            if (cmd.Pass != pass) continue;
            if (predator_pass != cmd.IsPredator) continue;
            if (cmd.UnitGroupID < 0) {
                non_composite.push_back(&cmd);
            } else {
                composite.push_back(&cmd);
            }
        }
        if (non_composite.empty() && composite.empty()) {
            return;
        }

        /**
         *  First-flush upload of the game-wide VPL light-remap. No-op
         *  after the first successful upload; `voxels.vpl` is static
         *  across the run.
         */
        LightRemapInstance.Ensure_Uploaded();

        /**
         *  Predator pass: ensure the per-frame SceneCopy snapshot exists.
         *  Idempotent — the SHP DistortionQueue may have already triggered
         *  it earlier in this pass.
         */
        if (predator_pass) {
            if (!SceneCopy::Get().Ensure_Copied(device)) {
                return;
            }
        }

        /**
         *  Non-composite path (single-section opaque voxels, shadows,
         *  bullets, anims). Batched by output target + shadow flag + mesh
         *  for minimal state changes.
         */
        if (!non_composite.empty()) {
            std::stable_sort(non_composite.begin(), non_composite.end(),
                [](const VoxelDrawCmd* a, const VoxelDrawCmd* b) {
                    if (a->OutputTarget != b->OutputTarget) {
                        return (uint8_t)a->OutputTarget < (uint8_t)b->OutputTarget;
                    }
                    if (a->IsShadow != b->IsShadow) {
                        return a->IsShadow && !b->IsShadow;
                    }
                    return a->Mesh < b->Mesh;
                });

            size_t bucket_start = 0;
            while (bucket_start < non_composite.size()) {
                const GpuRenderTarget bucket_target = non_composite[bucket_start]->OutputTarget;
                size_t bucket_end = bucket_start + 1;
                while (bucket_end < non_composite.size()
                    && non_composite[bucket_end]->OutputTarget == bucket_target) {
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
                    if (predator_pass) {
                        Issue_Predator_Cmd(device, *non_composite[i], target_w, target_h);
                    } else {
                        Issue_Cmd(device, *non_composite[i], target_w, target_h, is_sidebar);
                    }
                }

                bucket_start = bucket_end;
            }
        }

        /**
         *  Composite path: render each unit's sections to the shared scratch
         *  RT, then composite the scratch onto the scene as one quad with
         *  the unit's alpha. Avoids per-pixel depth-blend compounding for
         *  multi-section / translucent units (Stealth Tank chassis, etc).
         *  Groups are processed sequentially; the scratch is reused.
         */
        if (!composite.empty()) {
            std::stable_sort(composite.begin(), composite.end(),
                [](const VoxelDrawCmd* a, const VoxelDrawCmd* b) {
                    if (a->UnitGroupID != b->UnitGroupID) {
                        return a->UnitGroupID < b->UnitGroupID;
                    }
                    return a->Mesh < b->Mesh;
                });

            size_t i = 0;
            while (i < composite.size()) {
                const int group_id = composite[i]->UnitGroupID;
                size_t j = i + 1;
                while (j < composite.size() && composite[j]->UnitGroupID == group_id) {
                    ++j;
                }
                if (group_id >= 0 && (size_t)group_id < UnitGroups.size()) {
                    Flush_Composite_Group(device, &composite[i], j - i, UnitGroups[group_id]);
                }
                i = j;
            }
        }

        /**
         *  Unbind PS SRVs so subsequent passes start clean. Predator path
         *  uses slots 0..3 (palette, light-remap, normals, scene-copy);
         *  normal path uses 0..2. Clear 4 to cover both safely.
         */
        ID3D11ShaderResourceView* null_srvs[4] = {};
        device.Get_Context()->PSSetShaderResources(0, 4, null_srvs);
    }
}
