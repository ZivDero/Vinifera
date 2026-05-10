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
#include "shp_cache.h"
#include "tactical.h"
#include "tibsun_globals.h"

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

            RectF dst;
            dst.X = (float)(screen_x + fi->X) * xscale;
            dst.Y = (float)(screen_y + fi->Y) * yscale;
            dst.W = (float)fi->W * xscale;
            dst.H = (float)fi->H * yscale;
            const RectF src = { (float)fi->AtlasX, (float)fi->AtlasY,
                                 (float)fi->W,      (float)fi->H };

            /**
             *  One Begin/End per shape so each batch can carry its own
             *  AtlasSize uniform. Typical scenes have very few alpha shapes
             *  (<50) so the per-shape state-set overhead is negligible.
             */
            Batch.Begin(device, EBlend::Opaque, ESampler::PointClamp,
                        &AlphaEffect, bb_w, bb_h, EDepthStencil::None);

            AlphaWriteEffectParams params = {};
            params.AtlasSize[0] = (float)asset->Get_Atlas().Width();
            params.AtlasSize[1] = (float)asset->Get_Atlas().Height();
            AlphaEffect.Set_Params(device, params);

            const float identity_tint[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
            Batch.Draw(&asset->Get_Atlas(), dst, &src, identity_tint, 0.0f, 0.0f);
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

        bool has_pass_commands = false;
        for (const SpriteDrawCmd& cmd : Commands) {
            if (cmd.Pass == pass && cmd.Mode == SpriteDrawMode::Color) {
                has_pass_commands = true;
                break;
            }
        }
        if (!has_pass_commands) {
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
            if (a.ZAsset != b.ZAsset) return false;
            if (a.Palette != b.Palette) return false;
            if (a.EffectFlags != b.EffectFlags) return false;
            if (a.UseRemap != b.UseRemap) return false;
            if (a.WriteDepth != b.WriteDepth) return false;
            if (a.DisableDepth != b.DisableDepth) return false;
            if (a.UseRemap && memcmp(a.RemapTable, b.RemapTable, 16) != 0) return false;
            return true;
        };

        std::vector<SpriteDrawCmd> pass_commands;
        pass_commands.reserve(Commands.size());
        for (const SpriteDrawCmd& cmd : Commands) {
            /**
             *  Phase 4.1 Chunk A: only render `Color` mode commands. Alpha
             *  -buffer write modes (AlphaWriteAdd / AlphaWriteMult) require
             *  binding `AlphaRTV` instead of the backbuffer and a different
             *  blend formula — not yet implemented; a follow-up chunk hooks
             *  vanilla's `AlphaShapeClass` system and routes those commands
             *  through a dedicated alpha-write path.
             */
            if (cmd.Pass == pass && cmd.Mode == SpriteDrawMode::Color) {
                pass_commands.push_back(cmd);
            }
        }

        size_t i = 0;
        while (i < pass_commands.size()) {
            size_t j = i + 1;
            while (j < pass_commands.size() && state_eq(pass_commands[i], pass_commands[j])) {
                ++j;
            }

            const SpriteDrawCmd& head = pass_commands[i];
            head.Palette->Update_Remap(head.UseRemap ? head.RemapTable : nullptr);

            SpriteEffectParams params = {};
            params.AtlasSize[0] = (float)head.Asset->Get_Atlas().Width();
            params.AtlasSize[1] = (float)head.Asset->Get_Atlas().Height();
            if (head.ZAsset != nullptr) {
                params.ZShapeAtlasSize[0] = (float)head.ZAsset->Get_Atlas().Width();
                params.ZShapeAtlasSize[1] = (float)head.ZAsset->Get_Atlas().Height();
                params.ZShapeDepthScale = 1.0f / 16000.0f;
            }
            params.Flags = head.EffectFlags;
            if (head.UseRemap) {
                params.Flags |= SEF_USE_REMAP;
            }
            if (head.ZAsset != nullptr) {
                params.Flags |= SEF_USE_ZSHAPE;
            }

            /**
             *  SHAPE_DARKEN is a destination-multiply-by-0.5 op masked by the
             *  shape's non-zero pixels — the source color is irrelevant. Use
             *  the matching blend state for these groups; everything else
             *  stays on the standard premultiplied-alpha path.
             */
            const EBlend blend = (head.EffectFlags & SEF_DARKEN)
                ? EBlend::DestMultiplyHalf
                : EBlend::Premultiplied;

            /**
             *  Sprites depth-test against the shared depth buffer (which the
             *  tile pass populated). Most sprites don't write depth — preserves
             *  vanilla's submission-order layering for inter-sprite cases.
             *  Buildings (vanilla SHAPE_ZREADWRITE) write depth so units
             *  drawn afterwards behind them are correctly occluded.
             */
            /**
             *  UI overlays (selection brackets, pips, cameos drawn over the
             *  tactical view) use a constant per-sprite depth derived from
             *  the unit's foot, but their quad spans down into screen rows
             *  belonging to the next-front cell whose tile depth is *closer*
             *  than the sprite's. Depth-test would clip the bottom edge of
             *  these overlays. Vanilla doesn't z-test shapes that select a
             *  non-z blitter, so we mirror that by disabling depth here and
             *  relying on submission order for layering.
             */
            const EDepthStencil depth_state = head.DisableDepth
                ? EDepthStencil::None
                : (head.WriteDepth
                    ? EDepthStencil::WriteLessEqual
                    : EDepthStencil::TestLessEqual_NoWrite);
            Batch.Begin(device, blend, ESampler::PointClamp, &PalEffect, bb_w, bb_h,
                        depth_state);
            PalEffect.Bind_Palette(device, *head.Palette);
            PalEffect.Set_Params(device, params);
            ID3D11ShaderResourceView* z_srv = head.ZAsset != nullptr
                ? head.ZAsset->Get_Atlas().Get_SRV()
                : nullptr;
            device.Get_Context()->PSSetShaderResources(3, 1, &z_srv);

            /**
             *  Alpha buffer at PS slot 4. Populated for the frame by
             *  `Flush_Alpha_Lights`, sampled per-pixel by the sprite shader so
             *  buildings / units inside an alpha-light cone brighten the same
             *  way tiles do.
             */
            ID3D11ShaderResourceView* alpha_srv = device.Get_Alpha_SRV();
            device.Get_Context()->PSSetShaderResources(4, 1, &alpha_srv);

            for (size_t k = i; k < j; ++k) {
                const SpriteDrawCmd& c = pass_commands[k];
                const ShpFrameInfo* fi = c.Asset->Get_Frame(c.FrameIndex);
                if (fi == nullptr || fi->W <= 0 || fi->H <= 0) {
                    continue;
                }
                const RectF src = { (float)fi->AtlasX, (float)fi->AtlasY, (float)fi->W, (float)fi->H };
                Batch.Draw(&c.Asset->Get_Atlas(), c.Dst, &src, c.Tint,
                           c.DstZTop, c.DstZBottom,
                           c.ZAsset != nullptr ? &c.ZSrcUV : nullptr);
            }

            Batch.End(device);
            PerfMonitor::Get().Note_Sprite_Batch();
            PerfMonitor::Get().Note_Sprite_Draw_Call();
            i = j;
        }

        /**
         *  Unbind palette/remap/z-shape/alpha SRVs to keep subsequent passes clean.
         */
        ID3D11ShaderResourceView* null_srvs[5] = {};
        device.Get_Context()->PSSetShaderResources(0, 5, null_srvs);
    }
}
