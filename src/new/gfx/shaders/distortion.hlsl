cbuffer SpriteCB : register(b0)
{
    float4x4 ProjMtx;
};
cbuffer EffectCB : register(b1)
{
    float2 AtlasSize;
    float2 SceneSize;
};

struct VSIn {
    float3 pos    : POSITION;
    float2 uv     : TEXCOORD0;
    float2 zuv    : TEXCOORD1;   // unused
    float4 col    : COLOR0;      // col.r = blend, col.g = warp_offset_px
    uint   layer  : TEXCOORD2;
    uint   pflags : TEXCOORD3;   // unused
};
struct VSOut {
    float4 pos       : SV_Position;
    float2 uv        : TEXCOORD0;
    nointerpolation float blend  : COLOR0;
    nointerpolation float warp   : COLOR1;
    nointerpolation uint  layer  : TEXCOORD2;
};

VSOut VSMain(VSIn i)
{
    VSOut o;
    float4 p = mul(ProjMtx, float4(i.pos.xy, 0, 1));
    o.pos    = float4(p.x, p.y, i.pos.z, 1);
    o.uv     = i.uv;
    o.blend  = i.col.r;
    o.warp   = i.col.g;
    o.layer  = i.layer;
    return o;
}

Texture2D<uint>          Atlas      : register(t0);
Texture2DArray<float4>   PaletteArr : register(t1);
Texture2D<float4>        SceneCopy  : register(t2);
SamplerState             PointS     : register(s0);

float4 PSMain(VSOut v) : SV_Target
{
    int2 px = int2(v.uv * AtlasSize);
    uint idx = Atlas.Load(int3(px, 0));
    if (idx == 0) discard;
    float4 shp = PaletteArr.Load(int4((int)idx, 0, (int)v.layer, 0));
    /**
     * SV_Position.xy in the PS is the rasterized pixel center;
     * divide by the scene RT size to get a [0,1] UV. The warp is
     * a fixed horizontal offset in pixel units (matches vanilla's
     * `dest[warp_offset]` integer-step displacement). Saturate so
     * edges of the screen don't wrap into garbage.
     */
    float2 base_uv = v.pos.xy / SceneSize;
    float2 warp_uv = saturate(base_uv + float2(v.warp / SceneSize.x, 0));
    float4 bg = SceneCopy.Sample(PointS, warp_uv);
    bg.a = 1.0;
    /**
     * lerp(shp, bg, blend) — blend=0.75 mirrors vanilla 75% (more
     * background, less sprite); blend=0.25 mirrors vanilla 25%.
     */
    float4 c = lerp(shp, bg, v.blend);
    c.a = 1.0;
    return c;
}
