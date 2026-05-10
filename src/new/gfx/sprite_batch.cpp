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


namespace Vinifera::Gfx
{
    namespace
    {
        const char DefaultSpriteShaderHLSL[] =
            "cbuffer SpriteCB : register(b0) { float4x4 ProjMtx; };\n"
            "struct VSIn  { float3 pos : POSITION; float2 uv : TEXCOORD0; float2 zuv : TEXCOORD1; float4 col : COLOR0; };\n"
            "struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; float4 col : COLOR0; };\n"
            "VSOut VSMain(VSIn i) {\n"
            "    VSOut o;\n"
            "    float4 p = mul(ProjMtx, float4(i.pos.xy, 0, 1));\n"
            "    o.pos = float4(p.x, p.y, i.pos.z, 1);\n"
            "    o.uv  = i.uv;\n"
            "    o.col = i.col;\n"
            "    return o;\n"
            "}\n"
            "Texture2D    Tex : register(t0);\n"
            "SamplerState Smp : register(s0);\n"
            "float4 PSMain(VSOut v) : SV_Target { return Tex.Sample(Smp, v.uv) * v.col; }\n";

        const D3D11_INPUT_ELEMENT_DESC SpriteIL[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT,       0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 28, D3D11_INPUT_PER_VERTEX_DATA, 0 },
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

        VertexBuffer.Initialize(device.Get_Device(), device.Get_Context(), MaxQuads * 4);

        /**
         *  Build a static index buffer covering MaxQuads quads.
         */
        std::vector<unsigned short> indices(MaxQuads * 6);
        for (int q = 0; q < MaxQuads; ++q) {
            const unsigned short v = (unsigned short)(q * 4);
            indices[q * 6 + 0] = v + 0;
            indices[q * 6 + 1] = v + 1;
            indices[q * 6 + 2] = v + 2;
            indices[q * 6 + 3] = v + 0;
            indices[q * 6 + 4] = v + 2;
            indices[q * 6 + 5] = v + 3;
        }
        D3D11_BUFFER_DESC ib_desc = {};
        ib_desc.ByteWidth = (UINT)(indices.size() * sizeof(unsigned short));
        ib_desc.Usage = D3D11_USAGE_IMMUTABLE;
        ib_desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
        D3D11_SUBRESOURCE_DATA ib_data = {};
        ib_data.pSysMem = indices.data();
        if (FAILED(device.Get_Device()->CreateBuffer(&ib_desc, &ib_data, &IndexBuffer))) {
            DEBUG_ERROR("Gfx::SpriteBatch: index-buffer creation failed.\n");
            Shutdown();
            return false;
        }

        if (!Create_Default_Effect(device)) {
            Shutdown();
            return false;
        }
        return true;
    }


    bool SpriteBatch::Create_Default_Effect(GraphicsDevice& device)
    {
        return DefaultEffect.Initialize(
            device,
            DefaultSpriteShaderHLSL, sizeof(DefaultSpriteShaderHLSL) - 1,
            "sprite_default",
            SpriteIL, _countof(SpriteIL),
            sizeof(SpriteCB));
    }


    void SpriteBatch::Shutdown()
    {
        VertexBuffer.Shutdown();
        Safe_Release(IndexBuffer);
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
    }


    void SpriteBatch::Draw(Texture2D* texture, const RectF& dst, const RectF* src, uint32_t color, float z)
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
        float tint[4];
        Unpack_Color_To_Tint(color, tint);
        Draw(texture, dst, src, tint, z_top, z_bottom, z_uv);
    }


    void SpriteBatch::Draw(Texture2D* texture, const RectF& dst, const RectF* src,
                           const float tint[4], float z_top, float z_bottom)
    {
        Draw(texture, dst, src, tint, z_top, z_bottom, nullptr);
    }


    void SpriteBatch::Draw(Texture2D* texture, const RectF& dst, const RectF* src,
                           const float tint[4], float z_top, float z_bottom, const RectF* z_uv)
    {
        if (!BatchOpen || texture == nullptr || !dst.Is_Valid()) {
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

        const float t[4] = { tint[0], tint[1], tint[2], tint[3] };

        s.V[0] = { { dst.X,         dst.Y,         z_top    }, { u0, v0 }, { zu0, zv0 }, { t[0], t[1], t[2], t[3] } };
        s.V[1] = { { dst.X + dst.W, dst.Y,         z_top    }, { u1, v0 }, { zu1, zv0 }, { t[0], t[1], t[2], t[3] } };
        s.V[2] = { { dst.X + dst.W, dst.Y + dst.H, z_bottom }, { u1, v1 }, { zu1, zv1 }, { t[0], t[1], t[2], t[3] } };
        s.V[3] = { { dst.X,         dst.Y + dst.H, z_bottom }, { u0, v1 }, { zu0, zv1 }, { t[0], t[1], t[2], t[3] } };

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
         *  Upload all pending verts. We cap at MaxQuads per draw call; if the
         *  pending list exceeds MaxQuads, slice into chunks.
         */
        size_t total_quads = Pending.size();
        size_t consumed = 0;

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
        ctx->RSSetState(device.States().Get(ERasterizer::CullNone));

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

        while (consumed < total_quads) {
            const size_t batch_quads = (total_quads - consumed) < (size_t)MaxQuads
                ? (total_quads - consumed) : (size_t)MaxQuads;

            SpriteVertex* dst = VertexBuffer.Begin((int)batch_quads * 4);
            if (dst == nullptr) {
                break;
            }
            /**
             *  Group adjacent sprites by texture for fewer SRV binds.
             */
            for (size_t i = 0; i < batch_quads; ++i) {
                const PendingSprite& s = Pending[consumed + i];
                memcpy(dst + i * 4, s.V, sizeof(SpriteVertex) * 4);
            }
            VertexBuffer.End();

            ID3D11Buffer* vb = VertexBuffer.Get();
            UINT stride = sizeof(SpriteVertex);
            UINT offset = 0;
            ctx->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
            ctx->IASetIndexBuffer(IndexBuffer, DXGI_FORMAT_R16_UINT, 0);
            ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

            /**
             *  Walk the pending slice issuing a DrawIndexed per contiguous
             *  texture run.
             */
            size_t run_start = 0;
            Texture2D* run_tex = Pending[consumed].Tex;
            for (size_t i = 1; i <= batch_quads; ++i) {
                Texture2D* tex_now = (i < batch_quads) ? Pending[consumed + i].Tex : nullptr;
                if (i == batch_quads || tex_now != run_tex) {
                    Flush_Group(device, run_tex, (int)run_start * 4, (int)(i - run_start));
                    run_start = i;
                    run_tex = tex_now;
                }
            }

            consumed += batch_quads;
        }

        Pending.clear();
    }


    void SpriteBatch::Flush_Group(GraphicsDevice& device, Texture2D* texture,
                                  int vertex_offset, int quad_count)
    {
        if (quad_count <= 0 || texture == nullptr) {
            return;
        }
        ID3D11DeviceContext* ctx = device.Get_Context();
        ID3D11ShaderResourceView* srv = texture->Get_SRV();
        ctx->PSSetShaderResources(0, 1, &srv);
        ctx->DrawIndexed(quad_count * 6, 0, vertex_offset);
    }
}
