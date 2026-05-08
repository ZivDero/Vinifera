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
            "struct VSIn  { float2 pos : POSITION; float2 uv : TEXCOORD0; float4 col : COLOR0; };\n"
            "struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; float4 col : COLOR0; };\n"
            "VSOut VSMain(VSIn i) {\n"
            "    VSOut o;\n"
            "    o.pos = mul(ProjMtx, float4(i.pos, 0, 1));\n"
            "    o.uv  = i.uv;\n"
            "    o.col = i.col;\n"
            "    return o;\n"
            "}\n"
            "Texture2D    Tex : register(t0);\n"
            "SamplerState Smp : register(s0);\n"
            "float4 PSMain(VSOut v) : SV_Target { return Tex.Sample(Smp, v.uv) * v.col; }\n";

        const D3D11_INPUT_ELEMENT_DESC SpriteIL[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 8,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR",    0, DXGI_FORMAT_R8G8B8A8_UNORM,  0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0 },
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
                            Effect* effect, int target_w, int target_h)
    {
        ActiveEffect = (effect != nullptr) ? effect : &DefaultEffect;
        ActiveBlend = blend;
        ActiveSampler = sampler;
        TargetWidth  = target_w  > 0 ? target_w  : device.Get_Backbuffer_Width();
        TargetHeight = target_h > 0 ? target_h : device.Get_Backbuffer_Height();
        Pending.clear();
        BatchOpen = true;
    }


    void SpriteBatch::Draw(Texture2D* texture, const RectF& dst, const RectF* src, uint32_t color)
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

        PendingSprite s = {};
        s.Tex = texture;

        s.V[0] = { { dst.X,         dst.Y         }, { u0, v0 }, color };
        s.V[1] = { { dst.X + dst.W, dst.Y         }, { u1, v0 }, color };
        s.V[2] = { { dst.X + dst.W, dst.Y + dst.H }, { u1, v1 }, color };
        s.V[3] = { { dst.X,         dst.Y + dst.H }, { u0, v1 }, color };

        Pending.push_back(s);
    }


    void SpriteBatch::Draw(Texture2D* texture, float x, float y, uint32_t color)
    {
        if (texture == nullptr) {
            return;
        }
        RectF dst = { x, y, (float)texture->Width(), (float)texture->Height() };
        Draw(texture, dst, nullptr, color);
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

        const float blend_factor[4] = { 0, 0, 0, 0 };
        ctx->OMSetBlendState(device.States().Get(ActiveBlend), blend_factor, 0xFFFFFFFFu);
        ctx->OMSetDepthStencilState(device.States().Get(EDepthStencil::None), 0);
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
