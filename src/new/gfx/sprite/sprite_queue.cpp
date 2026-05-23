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

#include "shapeset.h"      // ShapeSet — must come before alphashape.h
#include "alphashape.h"
#include "debughandler.h"
#include "graphics_device.h"
#include "perf_monitor.h"
#include "shp_atlas.h"
#include "shp_cache.h"
#include "tactical.h"
#include "tibsun_globals.h"

#include <algorithm>


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
        if (!AlphaEffect.Initialize(device)) {
            PalEffect.Shutdown();
            Batch.Shutdown();
            return false;
        }
        Commands.reserve(2048);
        Initialized = true;
        return true;
    }


    void SpriteQueue::Shutdown()
    {
        AlphaEffect.Shutdown();
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
        PerfMonitor::Get().Note_Sprite_Submit();
    }


    void SpriteQueue::Clear()
    {
        Commands.clear();
    }


    void SpriteQueue::Flush(GraphicsDevice& device)
    {
        for (int pass = 0; pass < (int)RenderPass::Count; ++pass) {
            Flush_Pass(device, (RenderPass)pass);
        }
        Commands.clear();
    }


    void SpriteQueue::Flush_Alpha_Lights(GraphicsDevice& device)
    {
        if (!Initialized) {
            return;
        }

        ID3D11UnorderedAccessView* uav = device.Get_Alpha_UAV();
        if (uav == nullptr) {
            return;
        }
        if (AlphaShapes.Count() == 0) {
            return;
        }
        if (TacticalMap == nullptr) {
            return;
        }

        ID3D11DeviceContext* ctx = device.Get_Context();
        /**
         *  AlphaBuffer is sized to vanilla's logical render resolution
         *  (matches SceneRT). Dst coords here are in vanilla's screen-pixel
         *  space which is already logical-res, so no scale is needed.
         */
        const int target_w = device.Get_Logical_Width();
        const int target_h = device.Get_Logical_Height();
        const float xscale = 1.0f;
        const float yscale = 1.0f;

        /**
         *  Clip alpha-shape writes to TacticalRect so they don't scribble
         *  into the top tabs.shp bar / sidebar region of the alpha buffer
         *  (which is sized to the full LogicalSurface).
         */
        const RectF tactical_clip = {
            (float)TacticalRect.X,
            (float)TacticalRect.Y,
            (float)TacticalRect.Width,
            (float)TacticalRect.Height,
        };

        const int tac_pixel_x = TacticalMap->TacPixelX;
        const int tac_pixel_y = TacticalMap->TacPixelY;

        /**
         *  Bind AlphaUAV (no RTV / DSV). Detach AlphaSRV first in case a
         *  previous frame left it bound somewhere — D3D enforces "no SRV+UAV
         *  on the same resource simultaneously".
         */
        ID3D11ShaderResourceView* null_srvs[4] = {};
        ctx->PSSetShaderResources(0, 4, null_srvs);
        ctx->OMSetRenderTargetsAndUnorderedAccessViews(
            0, nullptr, nullptr, 0, 1, &uav, nullptr);

        int submitted = 0;
        for (int i = 0; i < AlphaShapes.Count(); ++i) {
            AlphaShapeClass* s = AlphaShapes[i];
            if (s == nullptr || s->field_2C) continue;

            ShapeSet* image = s->Image;
            if (image == nullptr) continue;

            /**
             *  AlphaShape's stored DrawRect (Size) is in vanilla's
             *  TacPixel-relative absolute pixel space (see object.cpp:1408).
             *  Convert to current-frame screen coords by replaying vanilla's
             *  Draw_In_Area math, then scale to backbuffer pixels.
             */
            const int screen_x = TacticalRect.X + s->Size.X - tac_pixel_x;
            const int screen_y = TacticalRect.Y + s->Size.Y - tac_pixel_y;

            ShpAsset* asset = ShpCache::Get().Get_Or_Load(device, image);
            if (asset == nullptr) continue;
            const ShpFrameInfo* fi = asset->Get_Frame(0);
            if (fi == nullptr || fi->W <= 0 || fi->H <= 0) continue;
            if (asset->Atlas_Page() < 0) continue;

            RectF dst;
            dst.X = (float)(screen_x + fi->X) * xscale;
            dst.Y = (float)(screen_y + fi->Y) * yscale;
            dst.W = (float)fi->W * xscale;
            dst.H = (float)fi->H * yscale;
            const RectF src = { (float)fi->AtlasX, (float)fi->AtlasY,
                                 (float)fi->W,      (float)fi->H };

            Texture2D& page_tex = ShpAtlas::Get().Get_Page(asset->Atlas_Page());

            /**
             *  One Begin/End per shape so each batch can carry its own
             *  AtlasSize uniform. Typical scenes have very few alpha shapes
             *  (<50) so the per-shape state-set overhead is negligible.
             */
            Batch.Begin(device, EBlend::Opaque, ESampler::PointClamp,
                        &AlphaEffect, target_w, target_h, EDepthStencil::None);

            AlphaWriteEffectParams params = {};
            params.AtlasSize[0] = (float)ShpAtlas::Get().Page_Width();
            params.AtlasSize[1] = (float)ShpAtlas::Get().Page_Height();
            AlphaEffect.Set_Params(device, params);

            const float identity_tint[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
            Batch.Draw(&page_tex, dst, &src, identity_tint, 0.0f, 0.0f,
                       /*z_uv*/ nullptr, &tactical_clip);
            Batch.End(device);
            ++submitted;
        }

        /**
         *  Detach UAV so subsequent passes can bind AlphaSRV / RTV without
         *  conflict. D3D errors if same resource is bound as both.
         */
        ID3D11UnorderedAccessView* null_uav = nullptr;
        ctx->OMSetRenderTargetsAndUnorderedAccessViews(
            0, nullptr, nullptr, 0, 1, &null_uav, nullptr);

        PerfMonitor::Get().Set_Alpha_Lights(submitted);
    }


    void SpriteQueue::Flush_Pass(GraphicsDevice& device, RenderPass pass)
    {
        if (!Initialized || Commands.empty()) {
            return;
        }

        // Alpha-buffer write modes are handled separately by Flush_Alpha_Lights.
        std::vector<SpriteDrawCmd> pass_commands;
        pass_commands.reserve(Commands.size());
        for (const SpriteDrawCmd& cmd : Commands) {
            if (cmd.Pass == pass && cmd.Mode == SpriteDrawMode::Color) {
                pass_commands.push_back(cmd);
            }
        }
        if (pass_commands.empty()) {
            return;
        }

        /**
         *  Bucket by output target (Scene / Sidebar / ...). Stable-sort so
         *  submission order within each bucket is preserved — depth ordering
         *  and overlap semantics still match vanilla's per-target paint order.
         */
        std::stable_sort(pass_commands.begin(), pass_commands.end(),
            [](const SpriteDrawCmd& a, const SpriteDrawCmd& b) {
                return (uint8_t)a.OutputTarget < (uint8_t)b.OutputTarget;
            });

        /**
         *  Inner contiguous-state grouping: walk a bucket in submission
         *  order, batching commands that share (atlas page, palette,
         *  EffectFlags, depth flags). Since ShpAtlas packs all SHPs into a
         *  shared paged texture, distinct SHP assets that land on the same
         *  page now share a batch — the dominant pre-atlas state break.
         *  `Set_Params` runs once per batch since `SpriteBatch` defers all
         *  DrawIndexed calls to `End()`.
         */
        const auto state_eq = [](const SpriteDrawCmd& a, const SpriteDrawCmd& b) {
            /**
             *  Palette and ZAsset page are per-vertex now: palette layer rides
             *  the vertex stream into the shared PaletteArray; the z-shape
             *  atlas is the same texture as the color atlas (so binding the
             *  same SRV at t3 is harmless when no z-shape is in use).
             *
             *  What remains as real pipeline state: SHP atlas page (still may
             *  span multiple pages on heavy content), depth-stencil state
             *  (WriteDepth / DisableDepth), and the SEF_DARKEN bit because
             *  darken draws select the stencil-dedup depth state to prevent
             *  overlapping shadows from compound-darkening into 0.25.
             */
            if (a.Asset->Atlas_Page() != b.Asset->Atlas_Page()) return false;
            if (a.WriteDepth != b.WriteDepth) return false;
            if (a.DisableDepth != b.DisableDepth) return false;
            if (((a.EffectFlags ^ b.EffectFlags) & SEF_DARKEN) != 0) return false;
            return true;
        };

        /**
         *  Classify the *first* differing field between two adjacent
         *  state-incompatible commands. Used purely for telemetry — order
         *  here mirrors state_eq so the histogram tells us which field is
         *  the load-bearing batch-breaker.
         */
        const auto classify_break = [](const SpriteDrawCmd& a, const SpriteDrawCmd& b) {
            if (a.Asset->Atlas_Page() != b.Asset->Atlas_Page()) {
                PerfMonitor::Get().Note_Sprite_Break_Page();
                return;
            }
            if (a.WriteDepth != b.WriteDepth || a.DisableDepth != b.DisableDepth) {
                PerfMonitor::Get().Note_Sprite_Break_Depth();
                return;
            }
        };

        size_t bucket_start = 0;
        bool seen_first_batch = false;
        while (bucket_start < pass_commands.size()) {
            const GpuRenderTarget bucket_target = pass_commands[bucket_start].OutputTarget;
            size_t bucket_end = bucket_start + 1;
            while (bucket_end < pass_commands.size()
                && pass_commands[bucket_end].OutputTarget == bucket_target) {
                ++bucket_end;
            }

            /**
             *  Reorder the bucket so SHAPE_DARKEN commands group at the front,
             *  preserving original relative order within each group. Without
             *  this, every per-object `shadow → body` transition would produce
             *  a batch break (state_eq fails on the SEF_DARKEN bit) — turning
             *  a single ObjectLayer pass into hundreds of micro-batches. With
             *  it, each bucket fires at most one SEF_DARKEN-driven break: one
             *  batch with DarkenDedup state for all shadows, one batch with
             *  the regular sprite state for the rest.
             *
             *  Per-pixel dedup still works because the stencil channel is
             *  cleared once per frame and persists across passes — drawing all
             *  shadows first within a pass writes stencil=1 at every shadowed
             *  pixel, and the same-pixel second darken (in this pass or a
             *  later pass) hits stencil=1 and is discarded.
             */
            std::stable_partition(
                pass_commands.begin() + bucket_start,
                pass_commands.begin() + bucket_end,
                [](const SpriteDrawCmd& c) { return (c.EffectFlags & SEF_DARKEN) != 0; });

            /**
             *  Crossing a bucket boundary is itself a batch break (different
             *  OutputTarget = different render-target bind). The first batch
             *  of the frame doesn't count.
             */
            if (seen_first_batch) {
                PerfMonitor::Get().Note_Sprite_Break_Bucket();
            }

            if (bucket_target == GpuRenderTarget::None) {
                static bool warned = false;
                if (!warned) {
                    DEBUG_WARNING("SpriteQueue: dropping %zu commands with OutputTarget::None.\n",
                        bucket_end - bucket_start);
                    warned = true;
                }
                bucket_start = bucket_end;
                continue;
            }

            Bind_Render_Target(device, bucket_target);

            const bool is_sidebar = (bucket_target == GpuRenderTarget::Sidebar);
            const int target_w = is_sidebar ? device.Get_Sidebar_Target_Width()  : device.Get_Logical_Width();
            const int target_h = is_sidebar ? device.Get_Sidebar_Target_Height() : device.Get_Logical_Height();

            size_t i = bucket_start;
            while (i < bucket_end) {
                /**
                 *  Each new in-bucket batch (after the first) was forced by
                 *  some field in state_eq differing between pass_commands[i-1]
                 *  and pass_commands[i]. Classify which one for telemetry.
                 */
                if (i > bucket_start) {
                    classify_break(pass_commands[i - 1], pass_commands[i]);
                }

                size_t j = i + 1;
                while (j < bucket_end && state_eq(pass_commands[i], pass_commands[j])) {
                    ++j;
                }

                const SpriteDrawCmd& head = pass_commands[i];

                /**
                 *  Atlas size is constant (all pages share PageSize); we
                 *  still write it into the per-batch CB. Z-shape uses the
                 *  same shared atlas, so ZShapeAtlasSize is the same value.
                 *  USE_ZSHAPE / DARKEN flags are per-vertex now; only
                 *  NO_ALPHA_BUFFER stays in the CB (per-bucket).
                 */
                SpriteEffectParams params = {};
                params.AtlasSize[0] = (float)ShpAtlas::Get().Page_Width();
                params.AtlasSize[1] = (float)ShpAtlas::Get().Page_Height();
                params.ZShapeAtlasSize[0] = (float)ShpAtlas::Get().Page_Width();
                params.ZShapeAtlasSize[1] = (float)ShpAtlas::Get().Page_Height();
                params.ZShapeDepthScale = 1.0f / 16000.0f;
                params.Flags = 0;
                if (is_sidebar) {
                    params.Flags |= SEF_NO_ALPHA_BUFFER;
                }

                /**
                 *  SidebarRT has no DSV (Bind_Sidebar_Target uses
                 *  DepthBinding::None). Force depth-off for the Sidebar
                 *  bucket so EDepthStencil::TestLessEqual against a null
                 *  DSV doesn't trip undefined behaviour. Submission order
                 *  preserves layer ordering, matching vanilla's CPU paint.
                 *
                 *  SHAPE_DARKEN (SEF_DARKEN) batches use the stencil-dedup
                 *  state so overlapping shadow shapes (cliff + bridge, etc.)
                 *  darken each pixel exactly once instead of compounding
                 *  multiplicatively. The state object still tests depth
                 *  (LessEqual, no write) for occlusion correctness; the
                 *  stencil channel handles the per-pixel dedup.
                 */
                EDepthStencil depth_state;
                if (is_sidebar) {
                    depth_state = EDepthStencil::None;
                } else if (head.EffectFlags & SEF_DARKEN) {
                    depth_state = EDepthStencil::DarkenDedup;
                } else {
                    depth_state = head.DisableDepth
                        ? EDepthStencil::None
                        : (head.WriteDepth
                            ? EDepthStencil::WriteLessEqual
                            : EDepthStencil::TestLessEqual_NoWrite);
                }

                Batch.Begin(device, EBlend::DualSourceBlend, ESampler::PointClamp, &PalEffect,
                            target_w, target_h, depth_state);
                PalEffect.Bind_Palette_Array(device);
                PalEffect.Set_Params(device, params);

                /**
                 *  Always bind the shared atlas at t3 (the z-shape SRV);
                 *  the shader gates the sample on per-vertex SEF_USE_ZSHAPE,
                 *  so binding it when nothing in the batch needs z is free.
                 */
                Texture2D& page_tex = ShpAtlas::Get().Get_Page(head.Asset->Atlas_Page());
                ID3D11ShaderResourceView* z_srv = page_tex.Get_SRV();
                device.Get_Context()->PSSetShaderResources(3, 1, &z_srv);

                ID3D11ShaderResourceView* alpha_srv = device.Get_Alpha_SRV();
                device.Get_Context()->PSSetShaderResources(4, 1, &alpha_srv);

                for (size_t k = i; k < j; ++k) {
                    const SpriteDrawCmd& c = pass_commands[k];
                    const ShpFrameInfo* fi = c.Asset->Get_Frame(c.FrameIndex);
                    if (fi == nullptr || fi->W <= 0 || fi->H <= 0) {
                        continue;
                    }
                    const RectF src = { (float)fi->AtlasX, (float)fi->AtlasY, (float)fi->W, (float)fi->H };
                    const uint32_t layer = (c.Palette != nullptr && c.Palette->Layer() >= 0)
                                         ? (uint32_t)c.Palette->Layer() : 0u;
                    uint32_t flags = c.EffectFlags;
                    if (c.ZAsset != nullptr) flags |= SEF_USE_ZSHAPE;
                    Batch.Draw(&page_tex, c.Dst, &src, c.Tint,
                               c.DstZTop, c.DstZBottom,
                               c.ZAsset != nullptr ? &c.ZSrcUV : nullptr,
                               c.Clip.Is_Valid() ? &c.Clip : nullptr,
                               layer, flags);
                }

                Batch.End(device);
                PerfMonitor::Get().Note_Sprite_Batch();
                PerfMonitor::Get().Note_Sprite_Draw_Call();
                seen_first_batch = true;
                i = j;
            }

            bucket_start = bucket_end;
        }

        /**
         *  Unbind palette/remap/z-shape/alpha SRVs to keep subsequent passes clean.
         */
        ID3D11ShaderResourceView* null_srvs[5] = {};
        device.Get_Context()->PSSetShaderResources(0, 5, null_srvs);
    }


    void SpriteQueue::Render_Sprite_Immediate(GraphicsDevice& device, const SpriteDrawCmd& cmd,
                                              int target_w, int target_h)
    {
        if (!Initialized) return;
        if (cmd.Asset == nullptr) return;
        const ShpFrameInfo* fi = cmd.Asset->Get_Frame(cmd.FrameIndex);
        if (fi == nullptr || fi->W <= 0 || fi->H <= 0) return;

        SpriteEffectParams params = {};
        params.AtlasSize[0] = (float)ShpAtlas::Get().Page_Width();
        params.AtlasSize[1] = (float)ShpAtlas::Get().Page_Height();
        params.ZShapeAtlasSize[0] = (float)ShpAtlas::Get().Page_Width();
        params.ZShapeAtlasSize[1] = (float)ShpAtlas::Get().Page_Height();
        params.ZShapeDepthScale = 1.0f / 16000.0f;
        /**
         *  Composite-replay targets the unit-scratch (no AlphaBuffer/DSV).
         *  Disable alpha-buffer sampling so the shader doesn't read garbage
         *  out of an unbound SRV. Match the sidebar path's flag.
         */
        params.Flags = SEF_NO_ALPHA_BUFFER;

        /**
         *  Honor the cmd's depth flags. Callers that want pure painter-order
         *  rendering (e.g. unit-scratch composite, which clears depth between
         *  records and uses submission order for layering) set
         *  `cmd.DisableDepth = true`. Callers that want normal depth-tested
         *  sprite behaviour leave the flags alone — same logic the regular
         *  Flush_Pass uses. SEF_DARKEN routes to the stencil-dedup state so
         *  overlapping darken shapes don't compound (mirrors Flush_Pass).
         */
        const EDepthStencil depth_state = (cmd.EffectFlags & SEF_DARKEN)
            ? EDepthStencil::DarkenDedup
            : (cmd.DisableDepth
                ? EDepthStencil::None
                : (cmd.WriteDepth
                    ? EDepthStencil::WriteLessEqual
                    : EDepthStencil::TestLessEqual_NoWrite));

        Batch.Begin(device, EBlend::DualSourceBlend, ESampler::PointClamp, &PalEffect,
                    target_w, target_h, depth_state);
        PalEffect.Bind_Palette_Array(device);
        PalEffect.Set_Params(device, params);

        Texture2D& page_tex = ShpAtlas::Get().Get_Page(cmd.Asset->Atlas_Page());
        ID3D11ShaderResourceView* z_srv = page_tex.Get_SRV();
        device.Get_Context()->PSSetShaderResources(3, 1, &z_srv);

        ID3D11ShaderResourceView* alpha_srv = device.Get_Alpha_SRV();
        device.Get_Context()->PSSetShaderResources(4, 1, &alpha_srv);

        const RectF src = { (float)fi->AtlasX, (float)fi->AtlasY, (float)fi->W, (float)fi->H };
        const uint32_t layer = (cmd.Palette != nullptr && cmd.Palette->Layer() >= 0)
                             ? (uint32_t)cmd.Palette->Layer() : 0u;
        uint32_t flags = cmd.EffectFlags;
        if (cmd.ZAsset != nullptr) flags |= SEF_USE_ZSHAPE;

        Batch.Draw(&page_tex, cmd.Dst, &src, cmd.Tint,
                   cmd.DstZTop, cmd.DstZBottom,
                   cmd.ZAsset != nullptr ? &cmd.ZSrcUV : nullptr,
                   cmd.Clip.Is_Valid() ? &cmd.Clip : nullptr,
                   layer, flags);
        Batch.End(device);
    }
}
