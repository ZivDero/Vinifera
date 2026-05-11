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
        const int bb_w = device.Get_Backbuffer_Width();
        const int bb_h = device.Get_Backbuffer_Height();
        const float xscale = (VideoWidth > 0) ? (float)bb_w / (float)VideoWidth  : 1.0f;
        const float yscale = (VideoHeight > 0) ? (float)bb_h / (float)VideoHeight : 1.0f;

        /**
         *  TacPixelX/TacPixelY live at offset 0x5C/0x60 in `Tactical` (verified
         *  via the disasm of `Get_Relative_Tactical_Position` at 0x00612D70).
         *  The TSpp wrapper exposes the slot as `field_5C` (an IsoCoordinate /
         *  Point2D); .X is TacPixelX, .Y is TacPixelY.
         */
        const int tac_pixel_x = TacticalMap->field_5C.X;
        const int tac_pixel_y = TacticalMap->field_5C.Y;

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
                        &AlphaEffect, bb_w, bb_h, EDepthStencil::None);

            AlphaWriteEffectParams params = {};
            params.AtlasSize[0] = (float)ShpAtlas::Get().Page_Width();
            params.AtlasSize[1] = (float)ShpAtlas::Get().Page_Height();
            AlphaEffect.Set_Params(device, params);

            const float identity_tint[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
            Batch.Draw(&page_tex, dst, &src, identity_tint, 0.0f, 0.0f);
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

        /**
         *  Phase 4.1 Chunk A: only render `Color` mode commands. Alpha-buffer
         *  write modes (AlphaWriteAdd / AlphaWriteMult) are handled by
         *  `Flush_Alpha_Lights`.
         */
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
            if (a.Asset->Atlas_Page() != b.Asset->Atlas_Page()) return false;
            const int az = a.ZAsset ? a.ZAsset->Atlas_Page() : -1;
            const int bz = b.ZAsset ? b.ZAsset->Atlas_Page() : -1;
            if (az != bz) return false;
            if (a.Palette != b.Palette) return false;
            if (a.EffectFlags != b.EffectFlags) return false;
            if (a.WriteDepth != b.WriteDepth) return false;
            if (a.DisableDepth != b.DisableDepth) return false;
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
                    DEBUG_WARNING("SpriteQueue: dropping %zu commands with OutputTarget::None.\n",
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

            size_t i = bucket_start;
            while (i < bucket_end) {
                size_t j = i + 1;
                while (j < bucket_end && state_eq(pass_commands[i], pass_commands[j])) {
                    ++j;
                }

                const SpriteDrawCmd& head = pass_commands[i];

                /**
                 *  All pages share a single PageSize, so AtlasSize is
                 *  constant across all batches once the atlas is up; we
                 *  still write it into the per-batch CB to avoid a
                 *  bind-state divergence between TileEffect (which actually
                 *  does vary atlas size) and SpriteEffect.
                 */
                SpriteEffectParams params = {};
                params.AtlasSize[0] = (float)ShpAtlas::Get().Page_Width();
                params.AtlasSize[1] = (float)ShpAtlas::Get().Page_Height();
                if (head.ZAsset != nullptr) {
                    params.ZShapeAtlasSize[0] = (float)ShpAtlas::Get().Page_Width();
                    params.ZShapeAtlasSize[1] = (float)ShpAtlas::Get().Page_Height();
                    params.ZShapeDepthScale = 1.0f / 16000.0f;
                }
                params.Flags = head.EffectFlags;
                if (is_sidebar) {
                    /**
                     *  Sidebar (and any future non-Scene bucket) must not
                     *  sample the scene-relative AlphaTex — its contents are
                     *  tactical alpha-light + shroud at backbuffer coords and
                     *  would leak through onto cameos / build-slot frames.
                     */
                    params.Flags |= SEF_NO_ALPHA_BUFFER;
                }
                if (head.ZAsset != nullptr) {
                    params.Flags |= SEF_USE_ZSHAPE;
                }

                const EBlend blend = (head.EffectFlags & SEF_DARKEN)
                    ? EBlend::DestMultiplyHalf
                    : EBlend::Premultiplied;

                /**
                 *  SidebarRT has no DSV (Bind_Sidebar_Target uses
                 *  DepthBinding::None). Force depth-off for the Sidebar
                 *  bucket so EDepthStencil::TestLessEqual against a null
                 *  DSV doesn't trip undefined behaviour. Submission order
                 *  preserves layer ordering, matching vanilla's CPU paint.
                 */
                EDepthStencil depth_state;
                if (is_sidebar) {
                    depth_state = EDepthStencil::None;
                } else {
                    depth_state = head.DisableDepth
                        ? EDepthStencil::None
                        : (head.WriteDepth
                            ? EDepthStencil::WriteLessEqual
                            : EDepthStencil::TestLessEqual_NoWrite);
                }

                Batch.Begin(device, blend, ESampler::PointClamp, &PalEffect, target_w, target_h,
                            depth_state);
                PalEffect.Bind_Palette(device, *head.Palette);
                PalEffect.Set_Params(device, params);
                ID3D11ShaderResourceView* z_srv = head.ZAsset != nullptr
                    ? ShpAtlas::Get().Get_Page(head.ZAsset->Atlas_Page()).Get_SRV()
                    : nullptr;
                device.Get_Context()->PSSetShaderResources(3, 1, &z_srv);

                ID3D11ShaderResourceView* alpha_srv = device.Get_Alpha_SRV();
                device.Get_Context()->PSSetShaderResources(4, 1, &alpha_srv);

                Texture2D& page_tex = ShpAtlas::Get().Get_Page(head.Asset->Atlas_Page());

                for (size_t k = i; k < j; ++k) {
                    const SpriteDrawCmd& c = pass_commands[k];
                    const ShpFrameInfo* fi = c.Asset->Get_Frame(c.FrameIndex);
                    if (fi == nullptr || fi->W <= 0 || fi->H <= 0) {
                        continue;
                    }
                    const RectF src = { (float)fi->AtlasX, (float)fi->AtlasY, (float)fi->W, (float)fi->H };
                    Batch.Draw(&page_tex, c.Dst, &src, c.Tint,
                               c.DstZTop, c.DstZBottom,
                               c.ZAsset != nullptr ? &c.ZSrcUV : nullptr,
                               c.Clip.Is_Valid() ? &c.Clip : nullptr);
                }

                Batch.End(device);
                PerfMonitor::Get().Note_Sprite_Batch();
                PerfMonitor::Get().Note_Sprite_Draw_Call();
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
}
