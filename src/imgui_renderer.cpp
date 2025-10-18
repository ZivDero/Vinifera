// imgui_dsurface_renderer.cpp
#include <vector>
#include <algorithm>
#include <cstdint>
#include <cmath>

#include "imgui.h"
#include "dsurface.h"

// ---------------------------------------------
// Simple ARGB32 texture for ImGui::TexID
struct DSurfImTexture {
    int w = 0, h = 0;
    std::vector<uint32_t> px; // ARGB32 (straight alpha)
};

static inline uint32_t SampleTexNearest(DSurfImTexture* t, float u, float v)
{
    if (!t || t->w <= 0 || t->h <= 0) return 0xFFFFFFFFu;
    // Clamp + nearest
    int x = (int)std::floor(u * t->w + 0.5f);
    int y = (int)std::floor(v * t->h + 0.5f);
    x = std::max(0, std::min(t->w - 1, x));
    y = std::max(0, std::min(t->h - 1, y));
    return t->px[y * t->w + x];
}

// ---------------------------------------------
// 32-bit alpha blend (src over dst) on ARGB
static inline uint32_t Blend32(uint32_t dst, uint32_t src)
{
    uint32_t sa = (src >> 24) & 0xFF;
    if (sa == 0)   return dst;
    if (sa == 255) return (src & 0x00FFFFFF) | 0xFF000000;

    uint32_t sr = (src >> 16) & 0xFF;
    uint32_t sg = (src >>  8) & 0xFF;
    uint32_t sb = (src >>  0) & 0xFF;

    uint32_t dr = (dst >> 16) & 0xFF;
    uint32_t dg = (dst >>  8) & 0xFF;
    uint32_t db = (dst >>  0) & 0xFF;

    uint32_t r = (sr * sa + dr * (255 - sa)) / 255;
    uint32_t g = (sg * sa + dg * (255 - sa)) / 255;
    uint32_t b = (sb * sa + db * (255 - sa)) / 255;
    return 0xFF000000 | (r << 16) | (g << 8) | b;
}

// ---------------------------------------------
// 16-bit helpers using DSurface's bit layout
static inline void Unpack16(uint16_t c, uint8_t& r, uint8_t& g, uint8_t& b)
{
    r = (uint8_t)(((c >> DSurface::RedRight)   & ((1 << (8 - DSurface::RedLeft))   - 1)) << DSurface::RedLeft);
    g = (uint8_t)(((c >> DSurface::GreenRight) & ((1 << (8 - DSurface::GreenLeft)) - 1)) << DSurface::GreenLeft);
    b = (uint8_t)(((c >> DSurface::BlueRight)  & ((1 << (8 - DSurface::BlueLeft))  - 1)) << DSurface::BlueLeft);
}

static inline uint16_t Pack16(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((r >> DSurface::RedLeft)   << DSurface::RedRight)   |
                      ((g >> DSurface::GreenLeft) << DSurface::GreenRight) |
                      ((b >> DSurface::BlueLeft)  << DSurface::BlueRight));
}

static inline uint16_t Blend16(uint16_t dst16, uint8_t sr, uint8_t sg, uint8_t sb, uint8_t sa)
{
    if (sa == 0)   return dst16;
    if (sa == 255) return Pack16(sr, sg, sb);

    uint8_t dr, dg, db;
    Unpack16(dst16, dr, dg, db);

    uint8_t r = (uint8_t)((sr * sa + dr * (255 - sa)) / 255);
    uint8_t g = (uint8_t)((sg * sa + dg * (255 - sa)) / 255);
    uint8_t b = (uint8_t)((sb * sa + db * (255 - sa)) / 255);
    return Pack16(r, g, b);
}

// ---------------------------------------------
// Barycentric triangle rasterizer with scissor
static inline float edge(float x0, float y0, float x1, float y1, float x, float y)
{
    return (x - x0) * (y1 - y0) - (y - y0) * (x1 - x0);
}

// NOTE: positions must be in framebuffer space (after DisplayPos/FramebufferScale applied)
static void RasterizeTri(const ImDrawVert& a, const ImDrawVert& b, const ImDrawVert& c,
                         uint8_t* base, int pitch, int bpp,
                         const RECT& sc, DSurfImTexture* tex)
{
    float x0=a.pos.x, y0=a.pos.y;
    float x1=b.pos.x, y1=b.pos.y;
    float x2=c.pos.x, y2=c.pos.y;

    float A = edge(x0,y0,x1,y1,x2,y2);
    if (A == 0.f) return;
    float invA = 1.0f / A;

    int minx = std::max((int)sc.left,   (int)std::floor(std::min({x0,x1,x2})));
    int maxx = std::min((int)sc.right,  (int)std::ceil (std::max({x0,x1,x2})));
    int miny = std::max((int)sc.top,    (int)std::floor(std::min({y0,y1,y2})));
    int maxy = std::min((int)sc.bottom, (int)std::ceil (std::max({y0,y1,y2})));
    if (minx >= maxx || miny >= maxy) return;

    auto unpack = [](ImU32 c, float out[4]){
        out[0] = ((c >> IM_COL32_R_SHIFT) & 0xFF) * (1.0f/255.0f);
        out[1] = ((c >> IM_COL32_G_SHIFT) & 0xFF) * (1.0f/255.0f);
        out[2] = ((c >> IM_COL32_B_SHIFT) & 0xFF) * (1.0f/255.0f);
        out[3] = ((c >> IM_COL32_A_SHIFT) & 0xFF) * (1.0f/255.0f);
    };
    float ca[4], cb[4], cc_[4];
    unpack(a.col, ca);
    unpack(b.col, cb);
    unpack(c.col, cc_);

    constexpr float eps = -0.0001f; // tolerate tiny negative due to FP error

    for (int y = miny; y < maxy; ++y) {
        float py = y + 0.5f;
        uint8_t* row = base + y * pitch;
        for (int x = minx; x < maxx; ++x) {
            float px = x + 0.5f;

            float w0 = edge(x1,y1,x2,y2,px,py) * invA;
            float w1 = edge(x2,y2,x0,y0,px,py) * invA;
            float w2 = edge(x0,y0,x1,y1,px,py) * invA;

            // accept both windings (normalized weights >= 0)
            if (w0 < eps || w1 < eps || w2 < eps)
                continue;

            float u = a.uv.x*w0 + b.uv.x*w1 + c.uv.x*w2;
            float v = a.uv.y*w0 + b.uv.y*w1 + c.uv.y*w2;

            float r = ca[0]*w0 + cb[0]*w1 + cc_[0]*w2;
            float g = ca[1]*w0 + cb[1]*w1 + cc_[1]*w2;
            float b = ca[2]*w0 + cb[2]*w1 + cc_[2]*w2;
            float a_ = ca[3]*w0 + cb[3]*w1 + cc_[3]*w2;

            uint32_t texel = SampleTexNearest(tex, u, v);
            uint8_t tr = (texel >> 16) & 0xFF;
            uint8_t tg = (texel >>  8) & 0xFF;
            uint8_t tb = (texel >>  0) & 0xFF;
            uint8_t ta = (texel >> 24) & 0xFF;

            float sr = (tr * (1.0f/255.0f)) * r;
            float sg = (tg * (1.0f/255.0f)) * g;
            float sb = (tb * (1.0f/255.0f)) * b;
            float sa = (ta * (1.0f/255.0f)) * a_;

            uint8_t SR = (uint8_t)(sr * 255.0f + 0.5f);
            uint8_t SG = (uint8_t)(sg * 255.0f + 0.5f);
            uint8_t SB = (uint8_t)(sb * 255.0f + 0.5f);
            uint8_t SA = (uint8_t)(sa * 255.0f + 0.5f);

            if (bpp == 4) {
                uint32_t* p = (uint32_t*)(row + x * 4);
                uint32_t dst = *p;
                uint32_t src = (SA << 24) | (SR << 16) | (SG << 8) | (SB);
                *p = Blend32(dst, src);
            } else if (bpp == 2) {
                uint16_t* p = (uint16_t*)(row + x * 2);
                *p = Blend16(*p, SR, SG, SB, SA);
            } else if (bpp == 1) {
                // Paletted/8bpp: simple luminance (no alpha)
                uint8_t Y = (uint8_t)((77*SR + 150*SG + 29*SB) / 256);
                row[x] = Y;
            }
        }
    }
}

// ---------------------------------------------
// Public API

void ImGuiDSurface_CreateFontsTexture()
{
    ImGuiIO& io = ImGui::GetIO();
    ImFontAtlas* atlas = io.Fonts;

    unsigned char* pixels = nullptr;
    int w=0, h=0;
    atlas->GetTexDataAsRGBA32(&pixels, &w, &h);

    auto* tex = new DSurfImTexture();
    tex->w = w; tex->h = h;
    tex->px.resize(size_t(w) * size_t(h));

    // RGBA (ImGui) -> ARGB (renderer)
    uint32_t* src = (uint32_t*)pixels;
    for (int i = 0; i < w*h; ++i) {
        uint32_t RGBA = src[i];
        uint8_t r = (RGBA >> 0)  & 0xFF;
        uint8_t g = (RGBA >> 8)  & 0xFF;
        uint8_t b = (RGBA >> 16) & 0xFF;
        uint8_t a = (RGBA >> 24) & 0xFF;
        tex->px[i] = (uint32_t(a) << 24) | (uint32_t(r) << 16) | (uint32_t(g) << 8) | b;
    }

#if defined(IMGUI_VERSION_NUM) && IMGUI_VERSION_NUM >= 19100
    atlas->SetTexID((ImTextureID)(uintptr_t)tex);
    if (atlas->TexRef._TexData)
        atlas->TexRef._TexData->BackendUserData = tex;
#else
    atlas->TexID = (ImTextureID)(uintptr_t)tex;
#endif
}

void ImGuiDSurface_DestroyFontsTexture()
{
    ImGuiIO& io = ImGui::GetIO();
    ImFontAtlas* atlas = io.Fonts;
    if (!atlas) return;

#if defined(IMGUI_VERSION_NUM) && IMGUI_VERSION_NUM >= 19100
    ImTextureRef texref = atlas->TexRef;
    if (texref._TexData && texref._TexData->BackendUserData) {
        delete (DSurfImTexture*)texref._TexData->BackendUserData;
        texref._TexData->BackendUserData = nullptr;
    } else if (texref._TexID != ImTextureID_Invalid) {
        delete (DSurfImTexture*)(uintptr_t)texref._TexID;
    }
    atlas->SetTexID(ImTextureID_Invalid);
    atlas->TexRef = ImTextureRef();
#else
    if (atlas->TexID) {
        delete (DSurfImTexture*)(uintptr_t)atlas->TexID;
        atlas->TexID = nullptr;
    }
#endif
}

// Render ImGui into a locked DSurface. Assumes surface size == io.DisplaySize.
void ImGuiDSurface_Render(ImDrawData* draw_data, Surface* surface)
{
    if (!draw_data || !surface) return;

    uint8_t* base = (uint8_t*)surface->Lock();
    if (!base) return;

    const int bpp   = surface->Get_Bytes_Per_Pixel();
    const int pitch = surface->Get_Pitch();

    const int fb_w  = (int)draw_data->DisplaySize.x;
    const int fb_h  = (int)draw_data->DisplaySize.y;
    if (fb_w <= 0 || fb_h <= 0) { surface->Unlock(); return; }

    const ImVec2 clip_off   = draw_data->DisplayPos;
    const ImVec2 clip_scale = draw_data->FramebufferScale;

    for (int n = 0; n < draw_data->CmdListsCount; ++n) {
        const ImDrawList* cl = draw_data->CmdLists[n];
        const ImDrawVert* vtx = cl->VtxBuffer.Data;
        const ImDrawIdx*  idx = cl->IdxBuffer.Data;

        int idx_off = 0;
        for (int ci = 0; ci < cl->CmdBuffer.Size; ++ci) {
            const ImDrawCmd& cmd = cl->CmdBuffer[ci];

            // Scissor (framebuffer space)
            ImVec4 cr;
            cr.x = (cmd.ClipRect.x - clip_off.x) * clip_scale.x;
            cr.y = (cmd.ClipRect.y - clip_off.y) * clip_scale.y;
            cr.z = (cmd.ClipRect.z - clip_off.x) * clip_scale.x;
            cr.w = (cmd.ClipRect.w - clip_off.y) * clip_scale.y;

            RECT sc;
            sc.left   = (LONG)std::max(0,    (int)std::floor(cr.x));
            sc.top    = (LONG)std::max(0,    (int)std::floor(cr.y));
            sc.right  = (LONG)std::min(fb_w, (int)std::ceil (cr.z));
            sc.bottom = (LONG)std::min(fb_h, (int)std::ceil (cr.w));
            if (sc.right <= sc.left || sc.bottom <= sc.top) {
                idx_off += (int)cmd.ElemCount;
                continue;
            }

            // Resolve texture (modern ImGui)
            DSurfImTexture* tex = nullptr;
#if defined(IMGUI_VERSION_NUM) && IMGUI_VERSION_NUM >= 19100
            const ImTextureRef& texref = cmd.TexRef; // ImTextureRef
            if (texref._TexData && texref._TexData->BackendUserData)
                tex = (DSurfImTexture*)texref._TexData->BackendUserData;
            else if (texref._TexID != ImTextureID_Invalid)
                tex = (DSurfImTexture*)(uintptr_t)texref._TexID;
#else
            tex = (DSurfImTexture*)(uintptr_t)cmd.TextureId;
#endif

            // Draw triangles: transform positions to framebuffer space first
            for (unsigned int i = 0; i < cmd.ElemCount; i += 3) {
                ImDrawVert a = vtx[idx[idx_off + i + 0]];
                ImDrawVert b = vtx[idx[idx_off + i + 1]];
                ImDrawVert c = vtx[idx[idx_off + i + 2]];

                a.pos.x = (a.pos.x - clip_off.x) * clip_scale.x;
                a.pos.y = (a.pos.y - clip_off.y) * clip_scale.y;
                b.pos.x = (b.pos.x - clip_off.x) * clip_scale.x;
                b.pos.y = (b.pos.y - clip_off.y) * clip_scale.y;
                c.pos.x = (c.pos.x - clip_off.x) * clip_scale.x;
                c.pos.y = (c.pos.y - clip_off.y) * clip_scale.y;

                RasterizeTri(a, b, c, base, pitch, bpp, sc, tex);
            }
            idx_off += (int)cmd.ElemCount;
        }
    }

    surface->Unlock();
}
