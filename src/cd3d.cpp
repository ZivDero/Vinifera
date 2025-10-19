#include "cd3d.h"
#include "debughandler.h"
#include "dsurface.h"
#include "tibsun_globals.h"
#include "wwmath.h"

#define DDRAW_INIT_STRUCT(dxstruct)                                                                                                                                                                                                                                                                                            \
    ZeroMemory(&dxstruct, sizeof(dxstruct));                                                                                                                                                                                                                                                                                   \
    dxstruct.dwSize = sizeof(dxstruct);
#define DDRAW_INIT_STRUCT_PTR(dxstruct)                                                                                                                                                                                                                                                                                        \
    ZeroMemory(dxstruct, sizeof(*dxstruct));                                                                                                                                                                                                                                                                                   \
    dxstruct->dwSize = sizeof(*dxstruct);

CD3DTexture* D3DTextureOverride;
CD3DTexture* D3DRenderTexture;
CD3DTexture* D3DTempTexture;
CD3DTriangleBuffer* D3DTriangle;

LPDIRECTDRAW2 DirectDraw2Object;
LPDIRECT3D2 Direct3DObject;
LPDIRECT3DDEVICE2 Direct3DDevice;
LPDIRECT3DVIEWPORT2 Direct3DViewport;


static const int TextureModeCaps[] = {
    (DDSCAPS_VIDEOMEMORY | DDSCAPS_TEXTURE),
    (DDSCAPS_VIDEOMEMORY | DDSCAPS_ZBUFFER),
    (DDSCAPS_VIDEOMEMORY | DDSCAPS_TEXTURE),
};


CD3DTexture::CD3DTexture(int width, int height, int mode) : Mode(mode), TextureSurfaceDesc(), TextureSurfacePtr(nullptr), TexturePtr(nullptr)
{
    DDRAW_INIT_STRUCT(TextureSurfaceDesc);

    TextureSurfaceDesc.dwFlags = DDSD_WIDTH | DDSD_HEIGHT | DDSD_CAPS;
    TextureSurfaceDesc.ddsCaps.dwCaps = TextureModeCaps[mode];
    TextureSurfaceDesc.dwWidth = width;
    TextureSurfaceDesc.dwHeight = height;

    if (mode == 1) {
        TextureSurfaceDesc.dwFlags = DDSD_ZBUFFERBITDEPTH | DDSD_WIDTH | DDSD_HEIGHT | DDSD_CAPS;
        TextureSurfaceDesc.dwZBufferBitDepth = 16;
    }

    HRESULT ddrval = 0;

    if (!DirectDraw2Object) {
        DEBUG_INFO("CD3DTexture::CD3DTexture() - DirectDraw2Object is null!\n");
        return;
    }

    ddrval = DirectDraw2Object->CreateSurface(&TextureSurfaceDesc, &TextureSurfacePtr, nullptr);
    if (FAILED(ddrval)) {
        DEBUG_FATAL("CD3DTexture::CD3DTexture()\n\nCreateSurface failed with error code %08X\n", ddrval);
        return;
    }

    switch (mode) {
    case 1: {
        DDBLTFX ddbltfx;
        DDRAW_INIT_STRUCT(ddbltfx);
        ddbltfx.dwFillDepth = 0xFFFFFFFF;
        TextureSurfacePtr->Blt(nullptr, nullptr, nullptr, DDBLT_DEPTHFILL, &ddbltfx);
        break;
    }
    case 0:
    case 2:
        ddrval = TextureSurfacePtr->QueryInterface(IID_IDirect3DTexture2, (LPVOID*)&TexturePtr);
        if (FAILED(ddrval)) {
            DEBUG_FATAL("CD3DTexture::CD3DTexture()\n\nQueryInterface failed with error code %08X\n", ddrval);
            // return;
        }
        break;
    default:
        break;
    };
}


CD3DTexture::~CD3DTexture()
{
    if (TexturePtr) {
        TexturePtr->Release();
    }
    TexturePtr = nullptr;

    if (TextureSurfacePtr) {
        TextureSurfacePtr->Release();
    }
    TextureSurfacePtr = nullptr;
}


void CD3DTriangle::Set_Color(unsigned char red, unsigned char green, unsigned char blue)
{
    D3DCOLOR color = RGBA_MAKE(red, green, blue, 255);
    D3DCOLOR specular = RGBA_MAKE(0, 0, 0, 255);
    for (int i = 0; i < MAX_VERTEX_COUNT; ++i) {
        Vertexes[i].color = color;
        Vertexes[i].specular = specular;
    }
}


void CD3DTriangle::Set_Coords(int vertex, float sx, float sy, float sz, float tu, float tv)
{
    D3DTLVERTEX& vtx = Vertexes[vertex];
    vtx.sx = sx;
    vtx.sy = sy;
    vtx.sz = sz;
    vtx.tu = tu;
    vtx.tv = tv;
    vtx.rhw = 1.0;
}


CD3DTriangleBuffer::CD3DTriangleBuffer() : TriangleBuffer(), Count(0) {}


CD3DTriangleBuffer::~CD3DTriangleBuffer()
{
    Count = 0;
}


HRESULT CD3DTriangleBuffer::Blit(LPDIRECTDRAWSURFACE surface)
{
    static LPDIRECTDRAWSURFACE _last_surface = nullptr;

    int cnt = Count;
    Count = 0;

    if (!surface) {
        return -1;
    }

    if (!D3DTempTexture || !D3DTempTexture->Texture_Allocated()) {
        return -1;
    }

    if (!D3DRenderTexture || !D3DRenderTexture->Texture_Surface_Allocated()) {
        return -1;
    }

    HRESULT ddrval = 0;

    if (_last_surface != surface) {

        if (_last_surface) {
            ddrval = _last_surface->DeleteAttachedSurface(0, D3DRenderTexture->Get_Texture_Surface_Ptr());
            if (FAILED(ddrval)) {
                DEBUG_FATAL("CD3DTriangleBuffer::Blit()\n\nDeleteAttachedSurface failed with error code %08X\n", ddrval);
                return -1;
            }
            _last_surface = nullptr;
        }

        ddrval = surface->AddAttachedSurface(D3DRenderTexture->Get_Texture_Surface_Ptr());
        if (FAILED(ddrval)) {
            DEBUG_FATAL("CD3DTriangleBuffer::Blit()\n\nAddAttachedSurface failed with error code %08X\n", ddrval);
            return -1;
        }

        ddrval = Direct3DDevice->SetRenderTarget(surface, 0);
        if (FAILED(ddrval)) {
            DEBUG_FATAL("CD3DTriangleBuffer::Blit()\n\nSetRenderTarget failed with error code %08X\n", ddrval);
            return -1;
        }

        _last_surface = surface;
    }

    LPDIRECT3DTEXTURE2 texture = (D3DTextureOverride != nullptr ? D3DTextureOverride->Get_Texture_Ptr() : D3DTempTexture->Get_Texture_Ptr());

    D3DTEXTUREHANDLE hTexture;
    ddrval = texture->GetHandle(Direct3DDevice, &hTexture);
    if (FAILED(ddrval)) {
        DEBUG_FATAL("CD3DTriangleBuffer::Blit()\n\nGetHandle failed with error code %08X\n", ddrval);
        return -1;
    }

    ddrval = Direct3DDevice->SetRenderState(D3DRENDERSTATE_TEXTUREHANDLE, hTexture);
    if (FAILED(ddrval)) {
        DEBUG_FATAL("CD3DTriangleBuffer::Blit()\n\nSetRenderState(D3DRENDERSTATE_TEXTUREHANDLE) with error code %08X\n", ddrval);
        return -1;
    }

    ddrval = Direct3DDevice->SetRenderState(D3DRENDERSTATE_ZENABLE, TRUE);
    if (FAILED(ddrval)) {
        DEBUG_FATAL("CD3DTriangleBuffer::Blit()\n\nSetRenderState(D3DRENDERSTATE_ZENABLE) with error code %08X\n", ddrval);
        return -1;
    }

    ddrval = Direct3DDevice->SetRenderState(D3DRENDERSTATE_ZWRITEENABLE, FALSE);
    if (FAILED(ddrval)) {
        DEBUG_FATAL("CD3DTriangleBuffer::Blit()\n\nSetRenderState(D3DRENDERSTATE_ZWRITEENABLE) with error code %08X\n", ddrval);
        return -1;
    }

    ddrval = Direct3DDevice->SetRenderState(D3DRENDERSTATE_TEXTUREMAG, D3DFILTER_LINEAR);
    if (FAILED(ddrval)) {
        DEBUG_FATAL("CD3DTriangleBuffer::Blit()\n\nSetRenderState(D3DRENDERSTATE_TEXTUREMAG) with error code %08X\n", ddrval);
        return -1;
    }

    ddrval = Direct3DDevice->SetRenderState(D3DRENDERSTATE_SRCBLEND, D3DBLEND_ONE);
    if (FAILED(ddrval)) {
        DEBUG_FATAL("CD3DTriangleBuffer::Blit()\n\nSetRenderState(D3DRENDERSTATE_SRCBLEND) with error code %08X\n", ddrval);
        return -1;
    }

    ddrval = Direct3DDevice->SetRenderState(D3DRENDERSTATE_DESTBLEND, D3DBLEND_ONE);
    if (FAILED(ddrval)) {
        DEBUG_FATAL("CD3DTriangleBuffer::Blit()\n\nSetRenderState(D3DRENDERSTATE_DESTBLEND) with error code %08X\n", ddrval);
        return -1;
    }

    ddrval = Direct3DDevice->SetRenderState(D3DRENDERSTATE_SPECULARENABLE, FALSE);
    if (FAILED(ddrval)) {
        DEBUG_FATAL("CD3DTriangleBuffer::Blit()\n\nSetRenderState(D3DRENDERSTATE_SPECULARENABLE) with error code %08X\n", ddrval);
        return -1;
    }

    ddrval = Direct3DDevice->SetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, TRUE);
    if (FAILED(ddrval)) {
        DEBUG_FATAL("CD3DTriangleBuffer::Blit()\n\nSetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE) with error code %08X\n", ddrval);
        return -1;
    }

    ddrval = Direct3DDevice->SetRenderState(D3DRENDERSTATE_CULLMODE, D3DCULL_NONE);
    if (FAILED(ddrval)) {
        DEBUG_FATAL("CD3DTriangleBuffer::Blit()\n\nSetRenderState(D3DRENDERSTATE_CULLMODE) with error code %08X\n", ddrval);
        return -1;
    }

    for (int i = 0; i < cnt; ++i) {
        ddrval = Direct3DDevice->DrawPrimitive(D3DPT_TRIANGLELIST, D3DVT_TLVERTEX, &TriangleBuffer[i], CD3DTriangle::MAX_VERTEX_COUNT, 0);
        if (FAILED(ddrval)) {
            // DEBUG_FATAL("CD3DTriangleBuffer::Blit()\n\nDrawPrimitive(%s) with error code %08X\n", i, ddrval);
            break;
        }
    }

    return ddrval;
}


HRESULT CD3DTriangleBuffer::Add(CD3DTriangle* info)
{
    if (Count == ARRAYSIZE(TriangleBuffer)) {
        return 1;
    }
    std::memcpy(&TriangleBuffer[Count], info, sizeof(CD3DTriangle));
    ++Count;
    return 0;
}


HRESULT Direct3D_Check_Interface_Caps()
{
    HRESULT ddrval = 0;

    DDCAPS ddcaps;
    DDRAW_INIT_STRUCT(ddcaps);

    if (!DirectDraw2Object) {
        DEBUG_INFO("Direct3D_Check_Interface_Caps() - DirectDraw2Object is null!\n");
        return -1;
    }

    ddrval = DirectDraw2Object->GetCaps(&ddcaps, NULL);
    if (FAILED(ddrval)) {
        DEBUG_FATAL("Direct3D_Check_Interface_Caps()\n\nGetCaps failed with error code %08X\n", ddrval);
        return -1;
    }

    bool d3d = (ddcaps.dwCaps & DDCAPS_3D) != 0;
    bool bd16 = (ddcaps.dwZBufferBitDepths & DDBD_16) != 0;
    bool d3ddev = (ddcaps.dwZBufferBitDepths & DDSCAPS_3DDEVICE) != 0;
    bool d3dtex = (ddcaps.dwZBufferBitDepths & DDSCAPS_TEXTURE) != 0;
    bool z = (ddcaps.dwZBufferBitDepths & DDSCAPS_ZBUFFER) != 0;

    if (d3d && bd16 && d3ddev && d3dtex && z) {

        return 0;
    }

    return -1;
}


HRESULT Direct3D_Check_Device_Caps()
{
    D3DDEVICEDESC d3ddesc;
    D3DDEVICEDESC d3demudesc;
    DDRAW_INIT_STRUCT(d3ddesc);
    DDRAW_INIT_STRUCT(d3demudesc);

    HRESULT ddrval = 0;

    if (!Direct3DDevice) {
        DEBUG_INFO("Direct3D_Check_Device_Caps() - Direct3DDevice is null!\n");
        return -1;
    }

    ddrval = Direct3DDevice->GetCaps(&d3ddesc, &d3demudesc);
    if (FAILED(ddrval)) {
        DEBUG_FATAL("Direct3D_Check_Device_Caps()\n\nGetCaps failed with error code %08X\n", ddrval);
        return ddrval;
    }

    static const DWORD CHECKED_CAPS = D3DDD_COLORMODEL | D3DDD_DEVCAPS | D3DDD_DEVICERENDERBITDEPTH | D3DDD_DEVICEZBUFFERBITDEPTH | D3DDD_TRICAPS;

    if ((d3demudesc.dwFlags & CHECKED_CAPS) == CHECKED_CAPS && (d3demudesc.dcmColorModel & D3DCOLOR_RGB) != 0 && (d3demudesc.dwDevCaps & D3DDEVCAPS_DRAWPRIMTLVERTEX) != 0 && (d3demudesc.dwDevCaps & D3DDEVCAPS_TEXTUREVIDEOMEMORY) != 0 && (d3demudesc.dwDeviceRenderBitDepth & DDBD_16) != 0 &&
        (d3demudesc.dwDeviceZBufferBitDepth & DDBD_16) != 0 && (d3demudesc.dpcTriCaps.dwZCmpCaps & (D3DPCMPCAPS_LESSEQUAL | D3DPCMPCAPS_LESS)) != 0 && (d3demudesc.dpcTriCaps.dwSrcBlendCaps & D3DPBLENDCAPS_ONE) != 0 && (d3demudesc.dpcTriCaps.dwDestBlendCaps & D3DPBLENDCAPS_ONE) != 0 &&
        d3demudesc.dwMaxTextureWidth >= 256) {

        return -1;
    }

    return ddrval;
}


HRESULT Direct3D_Prep()
{
    HRESULT ddrval = 0;

    if (!DirectDrawObject) {
        DEBUG_ERROR("Direct3D_Prep() - DirectDrawObject is null!\n");
        return -1;
    }

    ddrval = DirectDrawObject->QueryInterface(IID_IDirectDraw2, (LPVOID*)&DirectDraw2Object);
    if (FAILED(ddrval)) {
        DEBUG_FATAL("Direct3D_Prep()\n\nQueryInterface(DirectDraw) failed with error code %08X\n", ddrval);
        DirectDraw2Object = nullptr;
        return -1;
    }

    ddrval = DirectDraw2Object->QueryInterface(IID_IDirect3D2, (LPVOID*)&Direct3DObject);
    if (FAILED(ddrval)) {
        DEBUG_FATAL("Direct3D_Prep()\n\nQueryInterface(Direct3D) failed with error code %08X\n", ddrval);
        DirectDraw2Object = nullptr;
        Direct3DObject = nullptr;
        return -1;
    }

    return Direct3D_Check_Interface_Caps();
}


HRESULT Direct3D_Init()
{
    DEBUG_INFO("Direct3D_Init(enter)\n");

    if (!Direct3DObject) {
        DEBUG_ERROR("Direct3D_Init() - Direct3DObject is null!\n");
        return -1;
    }

    LPDIRECTDRAWSURFACE comp_surface = CompositeSurface->Get_DD_Surface();

    HRESULT ddrval = 0;

    ddrval = Direct3DObject->CreateDevice(IID_IDirect3DHALDevice, comp_surface, &Direct3DDevice);
    if (FAILED(ddrval)) {

        DEBUG_FATAL("Direct3D_Init()\n\nCreateDevice() failed with error code %08X\n", ddrval);
        return -1;
    }

    if (Direct3D_Check_Device_Caps()) {
        return -1;
    }

    ddrval = Direct3DObject->CreateViewport(&Direct3DViewport, nullptr);
    if (FAILED(ddrval)) {
        DEBUG_FATAL("Direct3D_Init()\n\nCreateViewport() failed with error code %08X\n", ddrval);
        return -1;
    }

    ddrval = Direct3DDevice->AddViewport(Direct3DViewport);
    if (FAILED(ddrval)) {
        DEBUG_FATAL("Direct3D_Init()\n\nAddViewport() failed with error code %08X\n", ddrval);
        return -1;
    }

    DDSURFACEDESC dddesc;
    DDRAW_INIT_STRUCT(dddesc);
    ddrval = comp_surface->GetSurfaceDesc(&dddesc);
    if (FAILED(ddrval)) {
        DEBUG_FATAL("Direct3D_Init()\n\nGetSurfaceDesc() failed with error code %08X\n", ddrval);
        return -1;
    }

    D3DVIEWPORT2 d3dvp2;
    DDRAW_INIT_STRUCT(d3dvp2);
    d3dvp2.dwWidth = dddesc.dwWidth;
    d3dvp2.dwHeight = dddesc.dwHeight;
    d3dvp2.dvClipWidth = dddesc.dwWidth;
    d3dvp2.dvClipHeight = dddesc.dwHeight;
    d3dvp2.dvMaxZ = 1.0;

    ddrval = Direct3DViewport->SetViewport2(&d3dvp2);
    if (FAILED(ddrval)) {
        DEBUG_FATAL("Direct3D_Init()\n\nSetViewport2() failed with error code %08X\n", ddrval);
        return -1;
    }

    ddrval = Direct3DDevice->SetCurrentViewport(Direct3DViewport);
    if (FAILED(ddrval)) {
        DEBUG_FATAL("Direct3D_Init()\n\nSetCurrentViewport() failed with error code %08X\n", ddrval);
        return -1;
    }

    D3DTempTexture = new CD3DTexture(256, 256, 0);
    if (!D3DTempTexture) {
        DEBUG_ERROR("Direct3D_Init() - Failed to create D3DTempTexture!\n");
        return 1;
    }

    void* buffer = std::malloc(256 * 256 * 2); // 16bit
    if (!buffer) {
        return 2;
    }

    unsigned short* buff = (unsigned short*)buffer;
    for (int xx = 0; xx + 128 < 256; ++xx) {
        for (int yy = 0; yy + 128 < 256; ++yy) {
            unsigned val = unsigned(400) - (WWMath::Sqrtf(((xx * xx) + (yy * yy))) * 3.0f);
            *buff++ = DSurface::Build_Hicolor_Pixel(val, val, val);
        }
    }

    //if (FAILED(D3DTempTexture->Blit(buffer))) {
    //    DEBUG_ERROR("Direct3D_Init() - D3DTempTexture::Blit failed!\n");
    //    std::free(buffer);
    //    return 3;
    //}

    std::free(buffer);

    DDSURFACEDESC dddesc2;
    DDRAW_INIT_STRUCT(dddesc2);
    ddrval = comp_surface->GetSurfaceDesc(&dddesc2);
    if (FAILED(ddrval)) {
        DEBUG_FATAL("Direct3D_Init()\n\nGetSurfaceDesc() failed with error code %08X\n", ddrval);
        return -1;
    }

    D3DRenderTexture = new CD3DTexture(dddesc2.dwWidth, dddesc2.dwHeight, 1);
    if (!D3DRenderTexture) {
        DEBUG_ERROR("Direct3D_Init() - Failed to create D3DRenderTexture!\n");
        return -1;
    }

    D3DTriangle = new CD3DTriangleBuffer;
    if (!D3DTriangle) {
        DEBUG_ERROR("Direct3D_Init() - Failed to create D3DTriangle!\n");
        return -1;
    }

    DEBUG_INFO("Direct3D_Init() - Setup successful.\n");

    return ddrval;
}


void Direct3D_Release()
{
    delete D3DTriangle;
    D3DTriangle = nullptr;

    delete D3DTextureOverride;
    D3DTextureOverride = nullptr;

    delete D3DRenderTexture;
    D3DRenderTexture = nullptr;

    delete D3DTempTexture;
    D3DTempTexture = nullptr;

    if (Direct3DViewport) {
        Direct3DViewport->Release();
        Direct3DViewport = nullptr;
    }

    if (Direct3DDevice) {
        Direct3DDevice->Release();
        Direct3DDevice = nullptr;
    }

    if (Direct3DObject) {
        Direct3DObject->Release();
        Direct3DObject = nullptr;
    }

    if (DirectDraw2Object) {
        DirectDraw2Object->Release();
        DirectDraw2Object = nullptr;
    }
}
