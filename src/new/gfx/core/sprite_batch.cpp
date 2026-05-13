/*******************************************************************************
/*                 O P E N  S O U R C E  --  V I N I F E R A                  **
/*******************************************************************************
 *  @brief  Lightweight 2D sprite batcher.
 *
 *  SPDX-License-Identifier: GPL-3.0-or-later
 *  Copyright (c) 2020-2026 Vinifera contributors
 ******************************************************************************/

#include "always.h"

#include "sprite_batch.h"

#include "debughandler.h"
#include "gfx_utils.h"
#include "graphics_device.h"
#include "texture2d.h"

#include <algorithm>
#include <cmath>


namespace Vinifera::Gfx
{
    namespace
    {
        const D3D11_INPUT_ELEMENT_DESC SpriteIL[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT,       0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 28, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 2, DXGI_FORMAT_R32_UINT,           0, 44, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 3, DXGI_FORMAT_R32_UINT,           0, 48, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };
    }


    SpriteBatch::~SpriteBatch()
    {
        Shutdown();
    }


    bool SpriteBatch::Initialize(GraphicsDevice& device, int max_quads_per_batch)
    {
        if (max_quads_per_batch <= 0) max_quads_per_batch = 2048;
        MaxQuads = max_quads_per_batch;

        /**
         *  Vertex / index buffers are sized for the worst case (every
         *  pending entry being a 9-vertex / 24-index tile-cell grid).
         *  The dynamic buffers grow on demand if the batch exceeds the
         *  cap.
         */
        VertexBuffer.Initialize(device.Get_Device(), device.Get_Context(), MaxQuads * 9);
        IndexBuffer.Initialize(device.Get_Device(), device.Get_Context(), MaxQuads * 24);

        if (!Create_Default_Effect(device)) {
            Shutdown();
            return false;
        }
        return true;
    }


    bool SpriteBatch::Create_Default_Effect(GraphicsDevice& device)
    {
        return DefaultEffect.Initialize(device, "DEFAULT_SPRITE",
                                        SpriteIL, _countof(SpriteIL),
                                        sizeof(SpriteCB));
    }


    void SpriteBatch::Shutdown()
    {
        VertexBuffer.Shutdown();
        IndexBuffer.Shutdown();
        DefaultEffect.Shutdown();
        ActiveEffect = nullptr;
        Pending.clear();
        BatchOpen = false;
    }


    void SpriteBatch::Begin(GraphicsDevice& device, EBlend blend, ESampler sampler,
                            Effect* effect, int target_w, int target_h, EDepthStencil depth)
    {
        ActiveEffect = (effect != nullptr) ? effect : &DefaultEffect;
        ActiveBlend = blend;
        ActiveSampler = sampler;
        ActiveDepth = depth;
        TargetWidth  = target_w  > 0 ? target_w  : device.Get_Backbuffer_Width();
        TargetHeight = target_h > 0 ? target_h : device.Get_Backbuffer_Height();
        Pending.clear();
        BatchOpen = true;
    }


    namespace
    {
        inline void Unpack_Color_To_Tint(uint32_t color, float out[4])
        {
            out[0] = (float)((color >>  0) & 0xFFu) / 255.0f;
            out[1] = (float)((color >>  8) & 0xFFu) / 255.0f;
            out[2] = (float)((color >> 16) & 0xFFu) / 255.0f;
            out[3] = (float)((color >> 24) & 0xFFu) / 255.0f;
        }

        inline D3D11_RECT Full_Scissor(int width, int height)
        {
            D3D11_RECT out = {};
            out.left = 0;
            out.top = 0;
            out.right = width;
            out.bottom = height;
            return out;
        }

        inline D3D11_RECT Clip_To_Scissor(const RectF& clip, int width, int height)
        {
            D3D11_RECT out = {};
            out.left = std::max<LONG>(0, std::min<LONG>((LONG)std::floor(clip.X), width));
            out.top = std::max<LONG>(0, std::min<LONG>((LONG)std::floor(clip.Y), height));
            out.right = std::max<LONG>(0, std::min<LONG>((LONG)std::ceil(clip.X + clip.W), width));
            out.bottom = std::max<LONG>(0, std::min<LONG>((LONG)std::ceil(clip.Y + clip.H), height));
            return out;
        }

        inline D3D11_RECT Effective_Scissor(bool use_clip, const RectF& clip, int width, int height)
        {
            return use_clip ? Clip_To_Scissor(clip, width, height) : Full_Scissor(width, height);
        }

        inline bool Same_Scissor(const D3D11_RECT& lhs, const D3D11_RECT& rhs)
        {
            return lhs.left == rhs.left
                && lhs.top == rhs.top
                && lhs.right == rhs.right
                && lhs.bottom == rhs.bottom;
        }
    }


    void SpriteBatch::Draw(Texture2D* texture, const RectF& dst, const RectF* src,
                           uint32_t color, float z)
    {
        Draw(texture, dst, src, color, z, z);
    }


    void SpriteBatch::Draw(Texture2D* texture, const RectF& dst, const RectF* src,
                           uint32_t color, float z_top, float z_bottom)
    {
        Draw(texture, dst, src, color, z_top, z_bottom, nullptr);
    }


    void SpriteBatch::Draw(Texture2D* texture, const RectF& dst, const RectF* src,
                           uint32_t color, float z_top, float z_bottom, const RectF* z_uv)
    {
        Draw(texture, dst, src, color, z_top, z_bottom, z_uv, nullptr);
    }


    void SpriteBatch::Draw(Texture2D* texture, const RectF& dst, const RectF* src,
                           uint32_t color, float z_top, float z_bottom,
                           const RectF* z_uv, const RectF* clip)
    {
        float tint[4];
        Unpack_Color_To_Tint(color, tint);
        Draw(texture, dst, src, tint, z_top, z_bottom, z_uv, clip);
    }


    void SpriteBatch::Draw(Texture2D* texture, const RectF& dst, const RectF* src,
                           const float tint[4], float z_top, float z_bottom)
    {
        Draw(texture, dst, src, tint, z_top, z_bottom, nullptr);
    }


    void SpriteBatch::Draw(Texture2D* texture, const RectF& dst, const RectF* src,
                           const float tint[4], float z_top, float z_bottom, const RectF* z_uv)
    {
        Draw(texture, dst, src, tint, z_top, z_bottom, z_uv, nullptr);
    }


    void SpriteBatch::Draw(Texture2D* texture, const RectF& dst, const RectF* src,
                           const float tint[4], float z_top, float z_bottom,
                           const RectF* z_uv, const RectF* clip)
    {
        Draw(texture, dst, src, tint, z_top, z_bottom, z_uv, clip, /*layer*/ 0, /*flags*/ 0);
    }


    void SpriteBatch::Draw(Texture2D* texture, const RectF& dst, const RectF* src,
                           const float tint[4], float z_top, float z_bottom,
                           const RectF* z_uv, const RectF* clip,
                           uint32_t layer, uint32_t flags)
    {
        if (!BatchOpen || texture == nullptr || !dst.Is_Valid() || (clip != nullptr && !clip->Is_Valid())) {
            return;
        }

        float u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;
        if (src != nullptr && texture->Width() > 0 && texture->Height() > 0) {
            const float tw = (float)texture->Width();
            const float th = (float)texture->Height();
            u0 = src->X / tw;
            v0 = src->Y / th;
            u1 = (src->X + src->W) / tw;
            v1 = (src->Y + src->H) / th;
        }

        float zu0 = 0.0f, zv0 = 0.0f, zu1 = 0.0f, zv1 = 0.0f;
        if (z_uv != nullptr) {
            zu0 = z_uv->X;
            zv0 = z_uv->Y;
            zu1 = z_uv->X + z_uv->W;
            zv1 = z_uv->Y + z_uv->H;
        }

        PendingSprite s = {};
        s.Tex = texture;
        s.Mode = PendingMode::Quad;
        if (clip != nullptr) {
            s.Clip = *clip;
            s.UseClip = true;
        }

        const float t[4] = { tint[0], tint[1], tint[2], tint[3] };

        s.V[0] = { { dst.X,         dst.Y,         z_top    }, { u0, v0 }, { zu0, zv0 }, { t[0], t[1], t[2], t[3] }, layer, flags };
        s.V[1] = { { dst.X + dst.W, dst.Y,         z_top    }, { u1, v0 }, { zu1, zv0 }, { t[0], t[1], t[2], t[3] }, layer, flags };
        s.V[2] = { { dst.X + dst.W, dst.Y + dst.H, z_bottom }, { u1, v1 }, { zu1, zv1 }, { t[0], t[1], t[2], t[3] }, layer, flags };
        s.V[3] = { { dst.X,         dst.Y + dst.H, z_bottom }, { u0, v1 }, { zu0, zv1 }, { t[0], t[1], t[2], t[3] }, layer, flags };

        Pending.push_back(s);
    }


    void SpriteBatch::Draw_Tile_Cell(Texture2D* texture, const RectF& dst, const RectF* src,
                                     const float tint_c[4], const float tint_n[4],
                                     const float tint_e[4], const float tint_s[4],
                                     const float tint_w[4],
                                     float z_top, float z_bottom,
                                     const RectF* z_uv, const RectF* clip,
                                     uint32_t layer, uint32_t flags)
    {
        if (!BatchOpen || texture == nullptr || !dst.Is_Valid() || (clip != nullptr && !clip->Is_Valid())) {
            return;
        }

        float u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;
        if (src != nullptr && texture->Width() > 0 && texture->Height() > 0) {
            const float tw = (float)texture->Width();
            const float th = (float)texture->Height();
            u0 = src->X / tw;
            v0 = src->Y / th;
            u1 = (src->X + src->W) / tw;
            v1 = (src->Y + src->H) / th;
        }

        float zu0 = 0.0f, zv0 = 0.0f, zu1 = 0.0f, zv1 = 0.0f;
        if (z_uv != nullptr) {
            zu0 = z_uv->X;
            zv0 = z_uv->Y;
            zu1 = z_uv->X + z_uv->W;
            zv1 = z_uv->Y + z_uv->H;
        }

        /**
         *  3x3 vertex grid filling the rect: row-major TL/TM/TR / ML/CC/MR
         *  / BL/BM/BR. Full coverage of `dst` matches the quad path (so
         *  the rasterizer doesn't drop boundary pixels under the top-left
         *  fill rule); per-vertex tints at the four edge midpoints carry
         *  the cell's neighbour-blended lighting so the rasterizer
         *  produces a smooth gradient that meets the iso-diamond edges
         *  at (own+neighbour)/2. Depth interpolates linearly with Y.
         */
        const float xL = dst.X;
        const float xM = dst.X + dst.W * 0.5f;
        const float xR = dst.X + dst.W;
        const float yT = dst.Y;
        const float yC = dst.Y + dst.H * 0.5f;
        const float yB = dst.Y + dst.H;
        const float uM = (u0 + u1) * 0.5f;
        const float vM = (v0 + v1) * 0.5f;
        const float zuM = (zu0 + zu1) * 0.5f;
        const float zvM = (zv0 + zv1) * 0.5f;
        const float zMid = (z_top + z_bottom) * 0.5f;

        PendingSprite s = {};
        s.Tex = texture;
        s.Mode = PendingMode::TileCell;
        if (clip != nullptr) {
            s.Clip = *clip;
            s.UseClip = true;
        }

        /**
         *  Rect-corner tints fall outside the iso-diamond on standard
         *  tiles (alpha-discarded) so their exact value is invisible
         *  there; setting them to `tint_c` avoids needing diagonal
         *  neighbour samples and keeps any extra-rect / non-standard
         *  pixels at a sensible uniform colour.
         */
        s.V[0] = { { xL, yT, z_top  }, { u0, v0 }, { zu0, zv0 }, { tint_c[0], tint_c[1], tint_c[2], tint_c[3] }, layer, flags };
        s.V[1] = { { xM, yT, z_top  }, { uM, v0 }, { zuM, zv0 }, { tint_n[0], tint_n[1], tint_n[2], tint_n[3] }, layer, flags };
        s.V[2] = { { xR, yT, z_top  }, { u1, v0 }, { zu1, zv0 }, { tint_c[0], tint_c[1], tint_c[2], tint_c[3] }, layer, flags };
        s.V[3] = { { xL, yC, zMid   }, { u0, vM }, { zu0, zvM }, { tint_w[0], tint_w[1], tint_w[2], tint_w[3] }, layer, flags };
        s.V[4] = { { xM, yC, zMid   }, { uM, vM }, { zuM, zvM }, { tint_c[0], tint_c[1], tint_c[2], tint_c[3] }, layer, flags };
        s.V[5] = { { xR, yC, zMid   }, { u1, vM }, { zu1, zvM }, { tint_e[0], tint_e[1], tint_e[2], tint_e[3] }, layer, flags };
        s.V[6] = { { xL, yB, z_bottom }, { u0, v1 }, { zu0, zv1 }, { tint_c[0], tint_c[1], tint_c[2], tint_c[3] }, layer, flags };
        s.V[7] = { { xM, yB, z_bottom }, { uM, v1 }, { zuM, zv1 }, { tint_s[0], tint_s[1], tint_s[2], tint_s[3] }, layer, flags };
        s.V[8] = { { xR, yB, z_bottom }, { u1, v1 }, { zu1, zv1 }, { tint_c[0], tint_c[1], tint_c[2], tint_c[3] }, layer, flags };

        Pending.push_back(s);
    }


    void SpriteBatch::Draw(Texture2D* texture, float x, float y, uint32_t color, float z)
    {
        if (texture == nullptr) {
            return;
        }
        RectF dst = { x, y, (float)texture->Width(), (float)texture->Height() };
        Draw(texture, dst, nullptr, color, z);
    }


    void SpriteBatch::End(GraphicsDevice& device)
    {
        if (!BatchOpen) {
            return;
        }
        BatchOpen = false;

        if (Pending.empty() || ActiveEffect == nullptr) {
            return;
        }

        ID3D11DeviceContext* ctx = device.Get_Context();

        /**
         *  Update per-batch projection. Pixel-space ortho: (0,0) top-left ->
         *  (target_w, target_h) bottom-right.
         */
        const float L = 0.0f;
        const float R = (float)TargetWidth;
        const float T = 0.0f;
        const float B = (float)TargetHeight;
        SpriteCB cb = {};
        cb.ProjMtx[0]  = 2.0f / (R - L);
        cb.ProjMtx[5]  = 2.0f / (T - B);
        cb.ProjMtx[10] = 1.0f;
        cb.ProjMtx[12] = (R + L) / (L - R);
        cb.ProjMtx[13] = (T + B) / (B - T);
        cb.ProjMtx[15] = 1.0f;
        ActiveEffect->Set_Constants(device, &cb);

        /**
         *  Per-entry vertex / index counts. Quad = 4v / 6i; TileCell =
         *  9v / 24i (3x3 grid, 8 triangles). Both topologies emit
         *  TRIANGLELIST and live in the same dynamic VB / IB so a
         *  contiguous texture+scissor run can cover any mix with a
         *  single DrawIndexed.
         */
        auto entry_vert_count = [](const PendingSprite& s) -> int {
            return (s.Mode == PendingMode::TileCell) ? 9 : 4;
        };
        auto entry_index_count = [](const PendingSprite& s) -> int {
            return (s.Mode == PendingMode::TileCell) ? 24 : 6;
        };

        /**
         *  Blend factor matters only for blend states that use
         *  D3D11_BLEND_BLEND_FACTOR / INV_BLEND_FACTOR. DestMultiplyHalf is
         *  one such state (constant 0.5 for the destination-multiply path
         *  used by SHAPE_DARKEN); everything else ignores the factor.
         */
        float blend_factor[4] = { 0, 0, 0, 0 };
        if (ActiveBlend == EBlend::DestMultiplyHalf) {
            blend_factor[0] = blend_factor[1] = blend_factor[2] = blend_factor[3] = 0.5f;
        }
        ctx->OMSetBlendState(device.States().Get(ActiveBlend), blend_factor, 0xFFFFFFFFu);
        ctx->OMSetDepthStencilState(device.States().Get(ActiveDepth), 0);
        ctx->RSSetState(device.States().Get(ERasterizer::CullNoneScissor));

        /**
         *  Match the viewport to the target so that the pixel-space ortho
         *  projection lands correctly on screen.
         */
        D3D11_VIEWPORT vp = {};
        vp.Width  = (float)TargetWidth;
        vp.Height = (float)TargetHeight;
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;
        ctx->RSSetViewports(1, &vp);

        ID3D11SamplerState* sampler = device.States().Get(ActiveSampler);
        ctx->PSSetSamplers(0, 1, &sampler);

        ActiveEffect->Apply(device);

        /**
         *  Slice into chunks bounded by MaxQuads pending entries each so
         *  the dynamic buffers don't have to grow unbounded for one frame.
         *  (MaxQuads is now a pending-entry cap, not a quad cap.)
         */
        const size_t total_entries = Pending.size();
        size_t consumed = 0;
        while (consumed < total_entries) {
            const size_t batch_count = (total_entries - consumed) < (size_t)MaxQuads
                ? (total_entries - consumed) : (size_t)MaxQuads;

            int batch_verts = 0;
            int batch_indices = 0;
            for (size_t i = 0; i < batch_count; ++i) {
                const PendingSprite& s = Pending[consumed + i];
                batch_verts   += entry_vert_count(s);
                batch_indices += entry_index_count(s);
            }

            SpriteVertex*   vdst = VertexBuffer.Begin(batch_verts);
            unsigned short* idst = IndexBuffer.Begin(batch_indices);
            if (vdst == nullptr || idst == nullptr) {
                if (vdst != nullptr) VertexBuffer.End();
                if (idst != nullptr) IndexBuffer.End();
                break;
            }

            int v_off = 0;
            int i_off = 0;
            for (size_t i = 0; i < batch_count; ++i) {
                const PendingSprite& s = Pending[consumed + i];
                const int vc = entry_vert_count(s);
                memcpy(vdst + v_off, s.V, sizeof(SpriteVertex) * vc);

                const unsigned short base = (unsigned short)v_off;
                if (s.Mode == PendingMode::TileCell) {
                    /**
                     *  9-vertex 3x3 grid, row-major:
                     *    0=TL 1=TM 2=TR
                     *    3=ML 4=CC 5=MR
                     *    6=BL 7=BM 8=BR
                     *  Eight triangles, two per quadrant, with each
                     *  quadrant split along the iso-diamond edge (TM-ML
                     *  for top-left, TM-MR for top-right, ML-BM for
                     *  bottom-left, MR-BM for bottom-right). This puts
                     *  the rect-corner triangles entirely outside the
                     *  diamond (alpha-discarded) and keeps the four
                     *  C1-discontinuity diagonals along the diamond
                     *  edges — i.e. at the visible cell boundary, where
                     *  lighting genuinely transitions between cells, so
                     *  they don't read as artefacts inside the cell.
                     */
                    const unsigned short tl = base + 0, tm = base + 1, tr = base + 2;
                    const unsigned short ml = base + 3, cc = base + 4, mr = base + 5;
                    const unsigned short bl = base + 6, bm = base + 7, br = base + 8;
                    idst[i_off +  0] = tl; idst[i_off +  1] = tm; idst[i_off +  2] = ml;
                    idst[i_off +  3] = tm; idst[i_off +  4] = cc; idst[i_off +  5] = ml;
                    idst[i_off +  6] = tm; idst[i_off +  7] = tr; idst[i_off +  8] = mr;
                    idst[i_off +  9] = tm; idst[i_off + 10] = mr; idst[i_off + 11] = cc;
                    idst[i_off + 12] = ml; idst[i_off + 13] = bm; idst[i_off + 14] = bl;
                    idst[i_off + 15] = ml; idst[i_off + 16] = cc; idst[i_off + 17] = bm;
                    idst[i_off + 18] = mr; idst[i_off + 19] = br; idst[i_off + 20] = bm;
                    idst[i_off + 21] = mr; idst[i_off + 22] = bm; idst[i_off + 23] = cc;
                } else {
                    /** Quad: TL, TR, BR, BL → 0-1-2, 0-2-3. */
                    idst[i_off + 0] = base + 0; idst[i_off + 1] = base + 1; idst[i_off + 2] = base + 2;
                    idst[i_off + 3] = base + 0; idst[i_off + 4] = base + 2; idst[i_off + 5] = base + 3;
                }
                v_off += vc;
                i_off += entry_index_count(s);
            }
            VertexBuffer.End();
            IndexBuffer.End();

            ID3D11Buffer* vb = VertexBuffer.Get();
            UINT stride = sizeof(SpriteVertex);
            UINT offset = 0;
            ctx->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
            ctx->IASetIndexBuffer(IndexBuffer.Get(), DXGI_FORMAT_R16_UINT, 0);
            ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

            /**
             *  Walk the batch slice issuing a DrawIndexed per contiguous
             *  texture+scissor run. Track the index-range covered by each
             *  run since pending entries may have variable index counts.
             */
            int run_idx_start = 0;
            int run_idx_count = 0;
            Texture2D* run_tex = Pending[consumed].Tex;
            D3D11_RECT run_scissor = Effective_Scissor(
                Pending[consumed].UseClip, Pending[consumed].Clip, TargetWidth, TargetHeight);
            for (size_t i = 0; i < batch_count; ++i) {
                const PendingSprite& s = Pending[consumed + i];
                D3D11_RECT scissor_now = Effective_Scissor(s.UseClip, s.Clip, TargetWidth, TargetHeight);
                if (s.Tex != run_tex || !Same_Scissor(scissor_now, run_scissor)) {
                    Flush_Group(device, run_tex, run_scissor, run_idx_start, run_idx_count);
                    run_idx_start += run_idx_count;
                    run_idx_count = 0;
                    run_tex = s.Tex;
                    run_scissor = scissor_now;
                }
                run_idx_count += entry_index_count(s);
            }
            if (run_idx_count > 0) {
                Flush_Group(device, run_tex, run_scissor, run_idx_start, run_idx_count);
            }

            consumed += batch_count;
        }

        Pending.clear();
    }


    void SpriteBatch::Flush_Group(GraphicsDevice& device, Texture2D* texture, const D3D11_RECT& scissor,
                                  int index_offset, int index_count)
    {
        if (index_count <= 0 || texture == nullptr || scissor.right <= scissor.left || scissor.bottom <= scissor.top) {
            return;
        }
        ID3D11DeviceContext* ctx = device.Get_Context();
        ID3D11ShaderResourceView* srv = texture->Get_SRV();
        ctx->RSSetScissorRects(1, &scissor);
        ctx->PSSetShaderResources(0, 1, &srv);
        ctx->DrawIndexed((UINT)index_count, (UINT)index_offset, 0);
    }
}
