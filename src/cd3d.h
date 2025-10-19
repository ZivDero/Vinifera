#pragma once

#include "always.h"
#include <d3d.h>


class CD3DTexture
{
public:
    CD3DTexture(int width, int height, int mode);
    ~CD3DTexture();

    HRESULT Blit(void* buffer);

    LPDIRECTDRAWSURFACE Get_Texture_Surface_Ptr() const { return TextureSurfacePtr; }
    bool Texture_Surface_Allocated() const { return TextureSurfacePtr != nullptr; }
    LPDIRECT3DTEXTURE2 Get_Texture_Ptr() const { return TexturePtr; }
    bool Texture_Allocated() const { return TexturePtr != nullptr; }

private:
    int Mode;
    DDSURFACEDESC TextureSurfaceDesc;
    LPDIRECTDRAWSURFACE TextureSurfacePtr;
    LPDIRECT3DTEXTURE2 TexturePtr;
};


class CD3DTriangle
{
public:
    enum {
        MAX_VERTEX_COUNT = 3
    };

public:
    CD3DTriangle() : Vertexes() {}
    ~CD3DTriangle() {}

    void Set_Color(unsigned char red, unsigned char green, unsigned char blue);
    void Set_Coords(int vertex, float sx, float sy, float sz, float tu, float tv);

public:
    D3DTLVERTEX Vertexes[MAX_VERTEX_COUNT];
};


class CD3DTriangleBuffer
{
public:
    CD3DTriangleBuffer();
    ~CD3DTriangleBuffer();

    HRESULT Blit(LPDIRECTDRAWSURFACE surface);
    HRESULT Add(CD3DTriangle* info);

private:
    CD3DTriangle TriangleBuffer[1024];
    int Count;
};


HRESULT Direct3D_Check_Interface_Caps();
HRESULT Direct3D_Check_Device_Caps();
HRESULT Direct3D_Prep();
HRESULT Direct3D_Init();
void Direct3D_Release();

extern CD3DTexture* D3DTextureOverride;
extern CD3DTexture* D3DRenderTexture;
extern CD3DTexture* D3DTempTexture;
extern CD3DTriangleBuffer* D3DTriangle;

extern LPDIRECTDRAW2 DirectDraw2Object;
extern LPDIRECT3D2 Direct3DObject;
extern LPDIRECT3DDEVICE2 Direct3DDevice;
extern LPDIRECT3DVIEWPORT2 Direct3DViewport;
