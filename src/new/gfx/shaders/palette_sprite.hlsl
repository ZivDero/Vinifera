cbuffer SpriteCB : register(b0)
{
    float4x4 ProjMtx;
};
cbuffer EffectCB : register(b1)
{
    float2 AtlasSize;
    float2 ZShapeAtlasSize;
    float  ZShapeDepthScale;
    uint   Flags;
    uint2  _pad1;
};
static const uint SEF_DARKEN           = 0x02;
static const uint SEF_USE_ZSHAPE       = 0x20;
static const uint SEF_NO_ALPHA_BUFFER  = 0x40;

struct VSIn {
    float3 pos    : POSITION;
    float2 uv     : TEXCOORD0;
    float2 zuv    : TEXCOORD1;
    float4 col    : COLOR0;
    uint   layer  : TEXCOORD2;
    uint   pflags : TEXCOORD3;
};
struct VSOut {
    float4 pos    : SV_Position;
    float2 uv     : TEXCOORD0;
    float2 zuv    : TEXCOORD1;
    float4 col    : COLOR0;
    nointerpolation uint layer  : TEXCOORD2;
    nointerpolation uint pflags : TEXCOORD3;
};
VSOut VSMain(VSIn i)
{
    VSOut o;
    float4 p = mul(ProjMtx, float4(i.pos.xy, 0, 1));
    o.pos    = float4(p.x, p.y, i.pos.z, 1);
    o.uv     = i.uv;
    o.zuv    = i.zuv;
    o.col    = i.col;
    o.layer  = i.layer;
    o.pflags = i.pflags;
    return o;
}

Texture2D<uint>          Atlas      : register(t0);
Texture2DArray<float4>   PaletteArr : register(t1);
Texture2D<uint>          ZShape     : register(t3);
Texture2D<float>         AlphaTex   : register(t4);

struct PSOut {
    float4 color  : SV_Target0;
    float4 factor : SV_Target1;
    float  depth  : SV_Depth;
};
PSOut PSMain(VSOut v)
{
    PSOut o;
    o.depth = v.pos.z;
    int2 px = int2(v.uv * AtlasSize);
    uint idx = Atlas.Load(int3(px, 0));
    if (idx == 0) discard;
    if (v.pflags & SEF_USE_ZSHAPE) {
        float2 zpf = v.zuv * ZShapeAtlasSize;
        if (zpf.x >= 0.0 && zpf.y >= 0.0 && zpf.x < ZShapeAtlasSize.x && zpf.y < ZShapeAtlasSize.y) {
            uint zraw = ZShape.Load(int3(int2(zpf), 0)) & 0xFF;
            int zsigned = (zraw >= 128) ? (int)zraw - 256 : (int)zraw;
            o.depth = saturate(o.depth - (float)zsigned * ZShapeDepthScale);
        }
    }
    /**
     * SHAPE_DARKEN: shape acts as a mask. Dual-source blend with
     * src0 = 0 and src1 = 0.5 multiplies the destination by 0.5,
     * bit-identical to the old DestMultiplyHalf state.
     */
    if (v.pflags & SEF_DARKEN) {
        o.color  = float4(0, 0, 0, 0);
        o.factor = float4(0.5, 0.5, 0.5, 1);
        return o;
    }
    float4 c = PaletteArr.Load(int4((int)idx, 0, (int)v.layer, 0));
    c.rgb *= v.col.rgb;
    c.a   *= v.col.a;
    /**
     * Alpha-buffer modulation. Same formula as the tile shader:
     * 127 = neutral, 254 ~= 2x overbright, 0 = full dark.
     */
    if (!(Flags & SEF_NO_ALPHA_BUFFER)) {
        float alpha_byte = AlphaTex.Load(int3(int2(v.pos.xy), 0)) * 255.0;
        c.rgb *= alpha_byte / 127.0;
    }
    c.rgb *= c.a;     /* premultiply */
    o.color  = c;
    o.factor = float4(1.0 - c.a, 1.0 - c.a, 1.0 - c.a, 1.0 - c.a);
    return o;
}
