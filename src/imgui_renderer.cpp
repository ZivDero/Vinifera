#include "imgui_renderer.h"

#include "cd3d.h"
#include "debughandler.h"
#include "dsurface.h"
#include "tibsun_globals.h"

#include <algorithm>
#include <cstring>
#include <vector>

static ImGui_ImplD3D2_Data* g = nullptr;

LPDIRECTDRAWSURFACE g_imguiZ = nullptr;

// D3D2 TL vertex
struct TLV {
    float sx, sy, sz, rhw;
    DWORD diffuse;
    float tu, tv;
};

static inline DWORD RGBA8_to_ARGB8(ImU32 c)
{
    // ImGui packs as 0xAABBGGRR (on little-endian), use shifts:
    unsigned int r = (c >> IM_COL32_R_SHIFT) & 0xFF;
    unsigned int g = (c >> IM_COL32_G_SHIFT) & 0xFF;
    unsigned int b = (c >> IM_COL32_B_SHIFT) & 0xFF;
    unsigned int a = (c >> IM_COL32_A_SHIFT) & 0xFF;
    return (a << 24) | (r << 16) | (g << 8) | (b);
}

// --- Init / Shutdown -------------------------------------------------------

bool ImGui_ImplD3D2_Init()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    if (!Direct3DDevice) {
        DEBUG_INFO("ImGui_ImplD3D2_Init_FromExisting: No Direct3DDevice available\n");
        return false;
    }

    g = new ImGui_ImplD3D2_Data();
    g->dev = Direct3DDevice; // use existing
    g->dev->AddRef();        // keep ref

    g->viewport = Direct3DViewport;
    if (g->viewport) g->viewport->AddRef();

    return ImGui_ImplD3D2_CreateFontsTexture();
}

void ImGui_ImplD3D2_Shutdown()
{
    ImGui_ImplD3D2_DestroyFontsTexture();
    //if (g) {
    //    if (g->viewport) {
    //        g->dev->DeleteViewport(g->viewport); // optional, safe on D3D2
    //        g->viewport->Release();
    //        g->viewport = nullptr;
    //    }
    //    if (g->dev) g->dev->Release();
    //    if (g->d3d) g->d3d->Release();
    //    delete g;
    //    g = nullptr;
    //}
    //if (g_imguiZ) {
    //    g_imguiZ->Release();
    //    g_imguiZ = nullptr;
    //}
    ImGui::DestroyContext();
}

void ImGui_ImplD3D2_NewFrame()
{
    // nothing special, states are set in init and per-draw
}

// --- Font texture ----------------------------------------------------------
// Create ARGB4444 texture surface and upload ImGui font atlas (convert RGBA8->ARGB4444)
static inline WORD RGBA8_to_ARGB4444(ImU32 c)
{
    unsigned r = (c >> IM_COL32_R_SHIFT) & 0xFF;
    unsigned g8 = (c >> IM_COL32_G_SHIFT) & 0xFF;
    unsigned b = (c >> IM_COL32_B_SHIFT) & 0xFF;
    unsigned a = (c >> IM_COL32_A_SHIFT) & 0xFF;
    unsigned ra = (a >> 4) & 0x0F;
    unsigned rr = (r >> 4) & 0x0F;
    unsigned rg = (g8 >> 4) & 0x0F;
    unsigned rb = (b >> 4) & 0x0F;
    return (WORD)((ra << 12) | (rr << 8) | (rg << 4) | (rb));
}

bool ImGui_ImplD3D2_CreateFontsTexture()
{
    ImGuiIO& io = ImGui::GetIO();
    unsigned char* pixels;
    int w, h;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);

    // Use CD3DTexture to allocate in system/video memory
    CD3DTexture* tex = new CD3DTexture(w, h, 0);
    if (!tex->Texture_Allocated()) {
        DEBUG_ERROR("ImGui font CD3DTexture failed\n");
        delete tex;
        return false;
    }

    // Lock and upload ImGui font pixels
    DDSURFACEDESC desc = {sizeof(desc)};
    if (SUCCEEDED(tex->Get_Texture_Surface_Ptr()->Lock(nullptr, &desc, DDLOCK_WAIT, nullptr))) {
        auto* dst = reinterpret_cast<uint16_t*>(desc.lpSurface);
        int pitch = desc.lPitch / 2;
        const ImU32* src = (const ImU32*)pixels;

        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                ImU32 c = src[y * w + x];
                unsigned a = (c >> 24) & 0xFF;
                dst[y * pitch + x] = DSurface::Build_Hicolor_Pixel(a, a, a); // grayscale alpha
            }
        }
        tex->Get_Texture_Surface_Ptr()->Unlock(nullptr);
    }

    io.Fonts->SetTexID((ImTextureID)(intptr_t)tex->Get_Texture_Ptr());
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    io.Fonts->TexIsBuilt = true;
    return true;
}

void ImGui_ImplD3D2_DestroyFontsTexture()
{
    ImGuiIO& io = ImGui::GetIO();
    if (io.Fonts->TexData) {
        CD3DTexture* tex = (CD3DTexture*)(intptr_t)io.Fonts->TexData->TexID;
        delete tex;
        io.Fonts->SetTexID(0);
        io.Fonts->TexIsBuilt = false; // add this
    }
}

// --- Rendering -------------------------------------------------------------
static void SetViewportClipRect(const ImVec4& cr, int fb_w, int fb_h)
{
    if (!g || !g->viewport) return;

    D3DVIEWPORT2 vp = {};
    vp.dwSize = sizeof(vp);
    vp.dwX = (DWORD)std::max(0, (int)cr.x);
    vp.dwY = (DWORD)std::max(0, (int)cr.y);
    vp.dwWidth = (DWORD)std::max(0, (int)(cr.z - cr.x));
    vp.dwHeight = (DWORD)std::max(0, (int)(cr.w - cr.y));
    vp.dvClipX = 0.0f;
    vp.dvClipY = 0.0f;
    vp.dvClipWidth = (float)fb_w;
    vp.dvClipHeight = (float)fb_h;
    vp.dvMinZ = 0.0f;
    vp.dvMaxZ = 1.0f;

    g->viewport->SetViewport2(&vp);
}


void ImGui_ImplD3D2_RenderDrawData(ImDrawData* draw_data)
{
    if (!draw_data || !Direct3DDevice) return;

    Direct3DDevice->BeginScene();

    CD3DTriangle tri;
    CD3DTriangleBuffer batch;

    const ImVec2 clip_off = draw_data->DisplayPos;
    const ImVec2 clip_scale = draw_data->FramebufferScale;

    for (int n = 0; n < draw_data->CmdListsCount; n++) {
        const ImDrawList* cl = draw_data->CmdLists[n];
        const ImDrawVert* vtx = cl->VtxBuffer.Data;
        const ImDrawIdx* idx = cl->IdxBuffer.Data;

        int idx_offset = 0;
        for (int ci = 0; ci < cl->CmdBuffer.Size; ci++) {
            const ImDrawCmd& cmd = cl->CmdBuffer[ci];
            for (unsigned int i = 0; i < cmd.ElemCount; i += 3) {
                for (int j = 0; j < 3; j++) {
                    const ImDrawVert& v = vtx[idx[idx_offset + i + j]];
                    tri.Set_Coords(j, (v.pos.x - clip_off.x) * clip_scale.x, (v.pos.y - clip_off.y) * clip_scale.y, 0.0f, v.uv.x, v.uv.y);

                    tri.Vertexes[j].color = RGBA_MAKE((v.col >> 0) & 0xFF, (v.col >> 8) & 0xFF, (v.col >> 16) & 0xFF, (v.col >> 24) & 0xFF);
                }
                batch.Add(&tri);
            }
            idx_offset += (int)cmd.ElemCount;
        }
    }

    batch.Blit(CompositeSurface->Get_DD_Surface());
    Direct3DDevice->EndScene();
}
