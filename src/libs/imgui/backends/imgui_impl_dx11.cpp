// dear imgui: Renderer Backend for DirectX 11
// This needs to be used along with a Platform Backend (e.g. Win32).
//
// This file is a Vinifera-local reproduction of the upstream Dear ImGui
// DirectX 11 backend, kept compatible with the project's vendored imgui 1.92.x
// and the new ImTextureData lifecycle (ImGuiBackendFlags_RendererHasTextures).
//
// Implemented features:
//  [X] Renderer: User texture binding. Use 'ID3D11ShaderResourceView*' as ImTextureID.
//  [X] Renderer: Large meshes support (64k+ vertices) with 16-bit indices (ImGuiBackendFlags_RendererHasVtxOffset).
//  [X] Renderer: Texture updates support for dynamic font atlas (ImGuiBackendFlags_RendererHasTextures).
// Missing features:
//  [ ] Renderer: Multi-viewport support (multiple windows).

#include "imgui.h"
#ifndef IMGUI_DISABLE
#include "imgui_impl_dx11.h"

#include <stdint.h>
#include <stdio.h>

#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>


struct ImGui_ImplDX11_Data
{
    ID3D11Device*               pd3dDevice;
    ID3D11DeviceContext*        pd3dDeviceContext;
    IDXGIFactory*               pFactory;
    ID3D11Buffer*               pVB;
    ID3D11Buffer*               pIB;
    ID3D11VertexShader*         pVertexShader;
    ID3D11InputLayout*          pInputLayout;
    ID3D11Buffer*               pVertexConstantBuffer;
    ID3D11PixelShader*          pPixelShader;
    ID3D11SamplerState*         pFontSampler;
    ID3D11RasterizerState*      pRasterizerState;
    ID3D11BlendState*           pBlendState;
    ID3D11DepthStencilState*    pDepthStencilState;
    int                         VertexBufferSize;
    int                         IndexBufferSize;

    ImGui_ImplDX11_Data()       { memset((void*)this, 0, sizeof(*this)); VertexBufferSize = 5000; IndexBufferSize = 10000; }
};

struct VERTEX_CONSTANT_BUFFER_DX11
{
    float   mvp[4][4];
};

static ImGui_ImplDX11_Data* ImGui_ImplDX11_GetBackendData()
{
    return ImGui::GetCurrentContext() ? (ImGui_ImplDX11_Data*)ImGui::GetIO().BackendRendererUserData : nullptr;
}


static void ImGui_ImplDX11_SetupRenderState(ImDrawData* draw_data, ID3D11DeviceContext* device_context)
{
    ImGui_ImplDX11_Data* bd = ImGui_ImplDX11_GetBackendData();

    D3D11_VIEWPORT vp = {};
    vp.Width = draw_data->DisplaySize.x;
    vp.Height = draw_data->DisplaySize.y;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    vp.TopLeftX = vp.TopLeftY = 0;
    device_context->RSSetViewports(1, &vp);

    device_context->IASetInputLayout(bd->pInputLayout);
    UINT stride = sizeof(ImDrawVert);
    UINT offset = 0;
    device_context->IASetVertexBuffers(0, 1, &bd->pVB, &stride, &offset);
    device_context->IASetIndexBuffer(bd->pIB, sizeof(ImDrawIdx) == 2 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT, 0);
    device_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    device_context->VSSetShader(bd->pVertexShader, nullptr, 0);
    device_context->VSSetConstantBuffers(0, 1, &bd->pVertexConstantBuffer);
    device_context->PSSetShader(bd->pPixelShader, nullptr, 0);
    device_context->PSSetSamplers(0, 1, &bd->pFontSampler);
    device_context->GSSetShader(nullptr, nullptr, 0);
    device_context->HSSetShader(nullptr, nullptr, 0);
    device_context->DSSetShader(nullptr, nullptr, 0);
    device_context->CSSetShader(nullptr, nullptr, 0);

    const float blend_factor[4] = { 0.f, 0.f, 0.f, 0.f };
    device_context->OMSetBlendState(bd->pBlendState, blend_factor, 0xffffffff);
    device_context->OMSetDepthStencilState(bd->pDepthStencilState, 0);
    device_context->RSSetState(bd->pRasterizerState);
}


void ImGui_ImplDX11_RenderDrawData(ImDrawData* draw_data)
{
    if (draw_data->DisplaySize.x <= 0.0f || draw_data->DisplaySize.y <= 0.0f) {
        return;
    }

    ImGui_ImplDX11_Data* bd = ImGui_ImplDX11_GetBackendData();
    ID3D11DeviceContext* ctx = bd->pd3dDeviceContext;

    /**
     *  Catch up with texture updates.
     */
    if (draw_data->Textures != nullptr) {
        for (ImTextureData* tex : *draw_data->Textures) {
            if (tex->Status != ImTextureStatus_OK) {
                ImGui_ImplDX11_UpdateTexture(tex);
            }
        }
    }

    /**
     *  (Re)create vertex/index buffers if needed.
     */
    if (!bd->pVB || bd->VertexBufferSize < draw_data->TotalVtxCount) {
        if (bd->pVB) { bd->pVB->Release(); bd->pVB = nullptr; }
        bd->VertexBufferSize = draw_data->TotalVtxCount + 5000;
        D3D11_BUFFER_DESC desc = {};
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.ByteWidth = bd->VertexBufferSize * sizeof(ImDrawVert);
        desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (bd->pd3dDevice->CreateBuffer(&desc, nullptr, &bd->pVB) < 0) {
            return;
        }
    }
    if (!bd->pIB || bd->IndexBufferSize < draw_data->TotalIdxCount) {
        if (bd->pIB) { bd->pIB->Release(); bd->pIB = nullptr; }
        bd->IndexBufferSize = draw_data->TotalIdxCount + 10000;
        D3D11_BUFFER_DESC desc = {};
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.ByteWidth = bd->IndexBufferSize * sizeof(ImDrawIdx);
        desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (bd->pd3dDevice->CreateBuffer(&desc, nullptr, &bd->pIB) < 0) {
            return;
        }
    }

    /**
     *  Upload vertex/index data into a single contiguous GPU buffer.
     */
    D3D11_MAPPED_SUBRESOURCE vtx_resource = {};
    D3D11_MAPPED_SUBRESOURCE idx_resource = {};
    if (ctx->Map(bd->pVB, 0, D3D11_MAP_WRITE_DISCARD, 0, &vtx_resource) != S_OK) {
        return;
    }
    if (ctx->Map(bd->pIB, 0, D3D11_MAP_WRITE_DISCARD, 0, &idx_resource) != S_OK) {
        ctx->Unmap(bd->pVB, 0);
        return;
    }
    ImDrawVert* vtx_dst = (ImDrawVert*)vtx_resource.pData;
    ImDrawIdx* idx_dst = (ImDrawIdx*)idx_resource.pData;
    for (const ImDrawList* draw_list : draw_data->CmdLists) {
        memcpy(vtx_dst, draw_list->VtxBuffer.Data, draw_list->VtxBuffer.Size * sizeof(ImDrawVert));
        memcpy(idx_dst, draw_list->IdxBuffer.Data, draw_list->IdxBuffer.Size * sizeof(ImDrawIdx));
        vtx_dst += draw_list->VtxBuffer.Size;
        idx_dst += draw_list->IdxBuffer.Size;
    }
    ctx->Unmap(bd->pVB, 0);
    ctx->Unmap(bd->pIB, 0);

    /**
     *  Setup orthographic projection in the vertex CB. Y axis grows downward
     *  in screen-space; flip via the matrix.
     */
    {
        D3D11_MAPPED_SUBRESOURCE mapped_resource = {};
        if (ctx->Map(bd->pVertexConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_resource) != S_OK) {
            return;
        }
        VERTEX_CONSTANT_BUFFER_DX11* cb = (VERTEX_CONSTANT_BUFFER_DX11*)mapped_resource.pData;
        const float L = draw_data->DisplayPos.x;
        const float R = draw_data->DisplayPos.x + draw_data->DisplaySize.x;
        const float T = draw_data->DisplayPos.y;
        const float B = draw_data->DisplayPos.y + draw_data->DisplaySize.y;
        const float mvp[4][4] = {
            { 2.0f/(R-L),    0.0f,           0.0f,       0.0f },
            { 0.0f,          2.0f/(T-B),     0.0f,       0.0f },
            { 0.0f,          0.0f,           0.5f,       0.0f },
            { (R+L)/(L-R),   (T+B)/(B-T),    0.5f,       1.0f },
        };
        memcpy(&cb->mvp, mvp, sizeof(mvp));
        ctx->Unmap(bd->pVertexConstantBuffer, 0);
    }

    /**
     *  Backup state that will be modified.
     */
    struct BACKUP_DX11_STATE {
        UINT                        ScissorRectsCount, ViewportsCount;
        D3D11_RECT                  ScissorRects[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
        D3D11_VIEWPORT              Viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
        ID3D11RasterizerState*      RS;
        ID3D11BlendState*           BlendState;
        FLOAT                       BlendFactor[4];
        UINT                        SampleMask;
        UINT                        StencilRef;
        ID3D11DepthStencilState*    DepthStencilState;
        ID3D11ShaderResourceView*   PSShaderResource;
        ID3D11SamplerState*         PSSampler;
        ID3D11PixelShader*          PS;
        ID3D11VertexShader*         VS;
        ID3D11GeometryShader*       GS;
        UINT                        PSInstancesCount, VSInstancesCount, GSInstancesCount;
        ID3D11ClassInstance         *PSInstances[256], *VSInstances[256], *GSInstances[256];
        D3D11_PRIMITIVE_TOPOLOGY    PrimitiveTopology;
        ID3D11Buffer*               IndexBuffer, *VertexBuffer, *VSConstantBuffer;
        UINT                        IndexBufferOffset, VertexBufferStride, VertexBufferOffset;
        DXGI_FORMAT                 IndexBufferFormat;
        ID3D11InputLayout*          InputLayout;
    };
    BACKUP_DX11_STATE old = {};
    old.ScissorRectsCount = old.ViewportsCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    ctx->RSGetScissorRects(&old.ScissorRectsCount, old.ScissorRects);
    ctx->RSGetViewports(&old.ViewportsCount, old.Viewports);
    ctx->RSGetState(&old.RS);
    ctx->OMGetBlendState(&old.BlendState, old.BlendFactor, &old.SampleMask);
    ctx->OMGetDepthStencilState(&old.DepthStencilState, &old.StencilRef);
    ctx->PSGetShaderResources(0, 1, &old.PSShaderResource);
    ctx->PSGetSamplers(0, 1, &old.PSSampler);
    old.PSInstancesCount = old.VSInstancesCount = old.GSInstancesCount = 256;
    ctx->PSGetShader(&old.PS, old.PSInstances, &old.PSInstancesCount);
    ctx->VSGetShader(&old.VS, old.VSInstances, &old.VSInstancesCount);
    ctx->VSGetConstantBuffers(0, 1, &old.VSConstantBuffer);
    ctx->GSGetShader(&old.GS, old.GSInstances, &old.GSInstancesCount);
    ctx->IAGetPrimitiveTopology(&old.PrimitiveTopology);
    ctx->IAGetIndexBuffer(&old.IndexBuffer, &old.IndexBufferFormat, &old.IndexBufferOffset);
    ctx->IAGetVertexBuffers(0, 1, &old.VertexBuffer, &old.VertexBufferStride, &old.VertexBufferOffset);
    ctx->IAGetInputLayout(&old.InputLayout);

    /**
     *  Setup desired DX state and render command lists.
     */
    ImGui_ImplDX11_SetupRenderState(draw_data, ctx);

    int global_idx_offset = 0;
    int global_vtx_offset = 0;
    ImVec2 clip_off = draw_data->DisplayPos;
    for (const ImDrawList* draw_list : draw_data->CmdLists) {
        for (int cmd_i = 0; cmd_i < draw_list->CmdBuffer.Size; cmd_i++) {
            const ImDrawCmd* pcmd = &draw_list->CmdBuffer[cmd_i];
            if (pcmd->UserCallback != nullptr) {
                if (pcmd->UserCallback == ImDrawCallback_ResetRenderState) {
                    ImGui_ImplDX11_SetupRenderState(draw_data, ctx);
                } else {
                    pcmd->UserCallback(draw_list, pcmd);
                }
            } else {
                ImVec2 clip_min(pcmd->ClipRect.x - clip_off.x, pcmd->ClipRect.y - clip_off.y);
                ImVec2 clip_max(pcmd->ClipRect.z - clip_off.x, pcmd->ClipRect.w - clip_off.y);
                if (clip_max.x <= clip_min.x || clip_max.y <= clip_min.y) {
                    continue;
                }

                const D3D11_RECT r = { (LONG)clip_min.x, (LONG)clip_min.y, (LONG)clip_max.x, (LONG)clip_max.y };
                ctx->RSSetScissorRects(1, &r);

                ID3D11ShaderResourceView* texture_srv = (ID3D11ShaderResourceView*)pcmd->GetTexID();
                ctx->PSSetShaderResources(0, 1, &texture_srv);
                ctx->DrawIndexed(pcmd->ElemCount, pcmd->IdxOffset + global_idx_offset, pcmd->VtxOffset + global_vtx_offset);
            }
        }
        global_idx_offset += draw_list->IdxBuffer.Size;
        global_vtx_offset += draw_list->VtxBuffer.Size;
    }

    /**
     *  Restore modified state.
     */
    ctx->RSSetScissorRects(old.ScissorRectsCount, old.ScissorRects);
    ctx->RSSetViewports(old.ViewportsCount, old.Viewports);
    ctx->RSSetState(old.RS); if (old.RS) old.RS->Release();
    ctx->OMSetBlendState(old.BlendState, old.BlendFactor, old.SampleMask); if (old.BlendState) old.BlendState->Release();
    ctx->OMSetDepthStencilState(old.DepthStencilState, old.StencilRef); if (old.DepthStencilState) old.DepthStencilState->Release();
    ctx->PSSetShaderResources(0, 1, &old.PSShaderResource); if (old.PSShaderResource) old.PSShaderResource->Release();
    ctx->PSSetSamplers(0, 1, &old.PSSampler); if (old.PSSampler) old.PSSampler->Release();
    ctx->PSSetShader(old.PS, old.PSInstances, old.PSInstancesCount); if (old.PS) old.PS->Release();
    for (UINT i = 0; i < old.PSInstancesCount; i++) if (old.PSInstances[i]) old.PSInstances[i]->Release();
    ctx->VSSetShader(old.VS, old.VSInstances, old.VSInstancesCount); if (old.VS) old.VS->Release();
    ctx->VSSetConstantBuffers(0, 1, &old.VSConstantBuffer); if (old.VSConstantBuffer) old.VSConstantBuffer->Release();
    ctx->GSSetShader(old.GS, old.GSInstances, old.GSInstancesCount); if (old.GS) old.GS->Release();
    for (UINT i = 0; i < old.VSInstancesCount; i++) if (old.VSInstances[i]) old.VSInstances[i]->Release();
    ctx->IASetPrimitiveTopology(old.PrimitiveTopology);
    ctx->IASetIndexBuffer(old.IndexBuffer, old.IndexBufferFormat, old.IndexBufferOffset); if (old.IndexBuffer) old.IndexBuffer->Release();
    ctx->IASetVertexBuffers(0, 1, &old.VertexBuffer, &old.VertexBufferStride, &old.VertexBufferOffset); if (old.VertexBuffer) old.VertexBuffer->Release();
    ctx->IASetInputLayout(old.InputLayout); if (old.InputLayout) old.InputLayout->Release();
}


void ImGui_ImplDX11_UpdateTexture(ImTextureData* tex)
{
    ImGui_ImplDX11_Data* bd = ImGui_ImplDX11_GetBackendData();

    if (tex->Status == ImTextureStatus_WantCreate) {
        IM_ASSERT(tex->TexID == ImTextureID_Invalid && tex->BackendUserData == nullptr);
        IM_ASSERT(tex->Format == ImTextureFormat_RGBA32);

        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = tex->Width;
        desc.Height = tex->Height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        desc.CPUAccessFlags = 0;

        D3D11_SUBRESOURCE_DATA sub = {};
        sub.pSysMem = tex->GetPixels();
        sub.SysMemPitch = (UINT)tex->GetPitch();

        ID3D11Texture2D* tex2d = nullptr;
        if (bd->pd3dDevice->CreateTexture2D(&desc, &sub, &tex2d) != S_OK || tex2d == nullptr) {
            IM_ASSERT(0 && "ImGui_ImplDX11_UpdateTexture: CreateTexture2D failed.");
            return;
        }

        D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
        srv_desc.Format = desc.Format;
        srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srv_desc.Texture2D.MipLevels = 1;
        srv_desc.Texture2D.MostDetailedMip = 0;

        ID3D11ShaderResourceView* srv = nullptr;
        if (bd->pd3dDevice->CreateShaderResourceView(tex2d, &srv_desc, &srv) != S_OK || srv == nullptr) {
            tex2d->Release();
            IM_ASSERT(0 && "ImGui_ImplDX11_UpdateTexture: CreateShaderResourceView failed.");
            return;
        }

        tex->SetTexID((ImTextureID)(intptr_t)srv);
        tex->BackendUserData = tex2d;
        tex->SetStatus(ImTextureStatus_OK);
    } else if (tex->Status == ImTextureStatus_WantUpdates) {
        ID3D11ShaderResourceView* srv = (ID3D11ShaderResourceView*)(intptr_t)tex->TexID;
        ID3D11Texture2D* tex2d = (ID3D11Texture2D*)tex->BackendUserData;
        IM_ASSERT(srv != nullptr && tex2d != nullptr);
        IM_UNUSED(srv);
        for (ImTextureRect& r : tex->Updates) {
            D3D11_BOX box = {};
            box.left = r.x;
            box.top = r.y;
            box.front = 0;
            box.right = r.x + r.w;
            box.bottom = r.y + r.h;
            box.back = 1;
            bd->pd3dDeviceContext->UpdateSubresource(tex2d, 0, &box,
                tex->GetPixelsAt(r.x, r.y), (UINT)tex->GetPitch(), 0);
        }
        tex->SetStatus(ImTextureStatus_OK);
    } else if (tex->Status == ImTextureStatus_WantDestroy) {
        if (tex->TexID != ImTextureID_Invalid) {
            ID3D11ShaderResourceView* srv = (ID3D11ShaderResourceView*)(intptr_t)tex->TexID;
            if (srv != nullptr) {
                srv->Release();
            }
        }
        if (tex->BackendUserData != nullptr) {
            ((ID3D11Texture2D*)tex->BackendUserData)->Release();
        }
        tex->SetTexID(ImTextureID_Invalid);
        tex->BackendUserData = nullptr;
        tex->SetStatus(ImTextureStatus_Destroyed);
    }
}


void ImGui_ImplDX11_CreateDeviceObjects()
{
    ImGui_ImplDX11_Data* bd = ImGui_ImplDX11_GetBackendData();
    if (!bd->pd3dDevice) {
        return;
    }
    if (bd->pVertexShader) {
        ImGui_ImplDX11_InvalidateDeviceObjects();
    }

    /**
     *  By using D3DCompile() we introduce a dependency on d3dcompiler_xx.dll;
     *  the build links d3dcompiler explicitly so this is fine on Windows.
     */
    {
        static const char* vertex_shader =
            "cbuffer vertexBuffer : register(b0) \n"
            "{\n"
            "  float4x4 ProjectionMatrix; \n"
            "};\n"
            "struct VS_INPUT\n"
            "{\n"
            "  float2 pos : POSITION;\n"
            "  float2 uv  : TEXCOORD0;\n"
            "  float4 col : COLOR0;\n"
            "};\n"
            "\n"
            "struct PS_INPUT\n"
            "{\n"
            "  float4 pos : SV_POSITION;\n"
            "  float4 col : COLOR0;\n"
            "  float2 uv  : TEXCOORD0;\n"
            "};\n"
            "\n"
            "PS_INPUT main(VS_INPUT input)\n"
            "{\n"
            "  PS_INPUT output;\n"
            "  output.pos = mul( ProjectionMatrix, float4(input.pos.xy, 0.f, 1.f));\n"
            "  output.col = input.col;\n"
            "  output.uv  = input.uv;\n"
            "  return output;\n"
            "}";

        ID3DBlob* vertexShaderBlob = nullptr;
        if (FAILED(D3DCompile(vertex_shader, strlen(vertex_shader), nullptr, nullptr, nullptr, "main", "vs_4_0", 0, 0, &vertexShaderBlob, nullptr))) {
            return;
        }
        if (bd->pd3dDevice->CreateVertexShader(vertexShaderBlob->GetBufferPointer(), vertexShaderBlob->GetBufferSize(), nullptr, &bd->pVertexShader) != S_OK) {
            vertexShaderBlob->Release();
            return;
        }

        D3D11_INPUT_ELEMENT_DESC local_layout[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,   0, (UINT)offsetof(ImDrawVert, pos), D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,   0, (UINT)offsetof(ImDrawVert, uv),  D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR",    0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, (UINT)offsetof(ImDrawVert, col), D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };
        if (bd->pd3dDevice->CreateInputLayout(local_layout, 3, vertexShaderBlob->GetBufferPointer(), vertexShaderBlob->GetBufferSize(), &bd->pInputLayout) != S_OK) {
            vertexShaderBlob->Release();
            return;
        }
        vertexShaderBlob->Release();

        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth = sizeof(VERTEX_CONSTANT_BUFFER_DX11);
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        bd->pd3dDevice->CreateBuffer(&desc, nullptr, &bd->pVertexConstantBuffer);
    }

    {
        static const char* pixel_shader =
            "struct PS_INPUT\n"
            "{\n"
            "  float4 pos : SV_POSITION;\n"
            "  float4 col : COLOR0;\n"
            "  float2 uv  : TEXCOORD0;\n"
            "};\n"
            "sampler sampler0;\n"
            "Texture2D texture0;\n"
            "\n"
            "float4 main(PS_INPUT input) : SV_Target\n"
            "{\n"
            "  float4 out_col = input.col * texture0.Sample(sampler0, input.uv); \n"
            "  return out_col; \n"
            "}";

        ID3DBlob* pixelShaderBlob = nullptr;
        if (FAILED(D3DCompile(pixel_shader, strlen(pixel_shader), nullptr, nullptr, nullptr, "main", "ps_4_0", 0, 0, &pixelShaderBlob, nullptr))) {
            return;
        }
        if (bd->pd3dDevice->CreatePixelShader(pixelShaderBlob->GetBufferPointer(), pixelShaderBlob->GetBufferSize(), nullptr, &bd->pPixelShader) != S_OK) {
            pixelShaderBlob->Release();
            return;
        }
        pixelShaderBlob->Release();
    }

    {
        D3D11_BLEND_DESC desc = {};
        desc.AlphaToCoverageEnable = false;
        desc.RenderTarget[0].BlendEnable = true;
        desc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
        desc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        bd->pd3dDevice->CreateBlendState(&desc, &bd->pBlendState);
    }

    {
        D3D11_RASTERIZER_DESC desc = {};
        desc.FillMode = D3D11_FILL_SOLID;
        desc.CullMode = D3D11_CULL_NONE;
        desc.ScissorEnable = true;
        desc.DepthClipEnable = true;
        bd->pd3dDevice->CreateRasterizerState(&desc, &bd->pRasterizerState);
    }

    {
        D3D11_DEPTH_STENCIL_DESC desc = {};
        desc.DepthEnable = false;
        desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
        desc.DepthFunc = D3D11_COMPARISON_ALWAYS;
        desc.StencilEnable = false;
        desc.FrontFace.StencilFailOp = desc.FrontFace.StencilDepthFailOp = desc.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
        desc.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
        desc.BackFace = desc.FrontFace;
        bd->pd3dDevice->CreateDepthStencilState(&desc, &bd->pDepthStencilState);
    }

    {
        D3D11_SAMPLER_DESC desc = {};
        desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        desc.MipLODBias = 0.f;
        desc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
        desc.MinLOD = 0.f;
        desc.MaxLOD = 0.f;
        bd->pd3dDevice->CreateSamplerState(&desc, &bd->pFontSampler);
    }
}


void ImGui_ImplDX11_InvalidateDeviceObjects()
{
    ImGui_ImplDX11_Data* bd = ImGui_ImplDX11_GetBackendData();
    if (!bd->pd3dDevice) {
        return;
    }

    for (ImTextureData* tex : ImGui::GetPlatformIO().Textures) {
        if (tex->RefCount == 1) {
            tex->SetStatus(ImTextureStatus_WantDestroy);
            ImGui_ImplDX11_UpdateTexture(tex);
        }
    }

    if (bd->pFontSampler)           { bd->pFontSampler->Release(); bd->pFontSampler = nullptr; }
    if (bd->pIB)                    { bd->pIB->Release(); bd->pIB = nullptr; }
    if (bd->pVB)                    { bd->pVB->Release(); bd->pVB = nullptr; }
    if (bd->pBlendState)            { bd->pBlendState->Release(); bd->pBlendState = nullptr; }
    if (bd->pDepthStencilState)     { bd->pDepthStencilState->Release(); bd->pDepthStencilState = nullptr; }
    if (bd->pRasterizerState)       { bd->pRasterizerState->Release(); bd->pRasterizerState = nullptr; }
    if (bd->pPixelShader)           { bd->pPixelShader->Release(); bd->pPixelShader = nullptr; }
    if (bd->pVertexConstantBuffer)  { bd->pVertexConstantBuffer->Release(); bd->pVertexConstantBuffer = nullptr; }
    if (bd->pInputLayout)           { bd->pInputLayout->Release(); bd->pInputLayout = nullptr; }
    if (bd->pVertexShader)          { bd->pVertexShader->Release(); bd->pVertexShader = nullptr; }
}


bool ImGui_ImplDX11_Init(ID3D11Device* device, ID3D11DeviceContext* device_context)
{
    ImGuiIO& io = ImGui::GetIO();
    IMGUI_CHECKVERSION();
    IM_ASSERT(io.BackendRendererUserData == nullptr && "Already initialized a renderer backend!");

    /**
     *  Get the DXGI factory from the device for symmetry with upstream
     *  (multi-viewport implementations need it). We just hold a reference.
     */
    IDXGIDevice* pDXGIDevice = nullptr;
    IDXGIAdapter* pDXGIAdapter = nullptr;
    IDXGIFactory* pFactory = nullptr;

    if (device->QueryInterface(IID_PPV_ARGS(&pDXGIDevice)) == S_OK) {
        if (pDXGIDevice->GetParent(IID_PPV_ARGS(&pDXGIAdapter)) == S_OK) {
            if (pDXGIAdapter->GetParent(IID_PPV_ARGS(&pFactory)) == S_OK) {
                /* ok */
            }
        }
    }

    ImGui_ImplDX11_Data* bd = IM_NEW(ImGui_ImplDX11_Data)();
    io.BackendRendererUserData = (void*)bd;
    io.BackendRendererName = "imgui_impl_dx11";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;

    bd->pd3dDevice = device;
    bd->pd3dDeviceContext = device_context;
    bd->pFactory = pFactory;
    bd->pd3dDevice->AddRef();
    bd->pd3dDeviceContext->AddRef();

    if (pDXGIDevice) pDXGIDevice->Release();
    if (pDXGIAdapter) pDXGIAdapter->Release();

    return true;
}


void ImGui_ImplDX11_Shutdown()
{
    ImGui_ImplDX11_Data* bd = ImGui_ImplDX11_GetBackendData();
    IM_ASSERT(bd != nullptr && "No renderer backend to shutdown, or already shutdown?");
    ImGuiIO& io = ImGui::GetIO();
    ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();

    ImGui_ImplDX11_InvalidateDeviceObjects();
    if (bd->pFactory)               { bd->pFactory->Release(); }
    if (bd->pd3dDevice)             { bd->pd3dDevice->Release(); }
    if (bd->pd3dDeviceContext)      { bd->pd3dDeviceContext->Release(); }

    io.BackendRendererName = nullptr;
    io.BackendRendererUserData = nullptr;
    io.BackendFlags &= ~(ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures);
    platform_io.ClearRendererHandlers();
    IM_DELETE(bd);
}


void ImGui_ImplDX11_NewFrame()
{
    ImGui_ImplDX11_Data* bd = ImGui_ImplDX11_GetBackendData();
    IM_ASSERT(bd != nullptr && "Context or backend not initialized! Did you call ImGui_ImplDX11_Init()?");

    if (!bd->pVertexShader) {
        ImGui_ImplDX11_CreateDeviceObjects();
    }
}


#endif // #ifndef IMGUI_DISABLE
