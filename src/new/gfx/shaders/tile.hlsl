cbuffer SpriteCB : register(b0)
{
    float4x4 ProjMtx;
};
cbuffer EffectCB : register(b1)
{
    float2 AtlasSize;
    float  ZDataDepthScale;
    float  _pad;
};

struct VSIn  { float3 pos : POSITION; float2 uv : TEXCOORD0; float2 zuv : TEXCOORD1; float4 col : COLOR0; };
struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; float4 col : COLOR0; };

VSOut VSMain(VSIn i)
{
    VSOut o;
    float4 p = mul(ProjMtx, float4(i.pos.xy, 0, 1));
    o.pos = float4(p.x, p.y, i.pos.z, 1);
    o.uv  = i.uv;
    o.col = i.col;
    return o;
}

Texture2D<uint>   Atlas     : register(t0);
Texture2D<float4> Palette   : register(t1);
Texture2D<uint>   ZAtlas    : register(t2);
Texture2D<float>  AlphaTex  : register(t3);
Texture2D<float>  TintMaskT : register(t4);

struct PSOut { float4 color : SV_Target; float depth : SV_Depth; };

/**
 *  Smooth float lighting. Vanilla's CPU rasterizer bucketed the result
 *  into a discrete row of `AlphaLightingRemap` (8 levels untinted, 62
 *  tinted) which produced visible banding on lit alphas and per-cell
 *  brightness gradients. We do the same math in continuous float instead.
 *
 *  Inputs from the vertex stream:
 *    v.col.rgb = cell.RedTint / GreenTint / BlueTint (already / 1000)
 *    v.col.a   = cell.TileBrightness                 (already / 1000;
 *                1.0 = neutral, 2.0 = max overbright)
 *
 *  AlphaTex is R8_UNORM seeded to 127/255 each frame; AlphaShape writes
 *  lighter / darker bytes around lights and shroud. 127 = neutral pass.
 *
 *  Tint mask: `TintMaskT` mirrors vanilla `_default_mask` — palette
 *  indices flagged false skip the colored tint multiply (used to keep
 *  e.g. unit-shadow indices monochromatic). Stock TS marks everything
 *  true, so on a vanilla build this collapses to `base * tint * scale`.
 */
PSOut PSMain(VSOut v)
{
    int2 px = int2(v.uv * AtlasSize);
    uint idx = Atlas.Load(int3(px, 0));
    if (idx == 0) discard;

    float3 base       = Palette.Load(int3((int)idx, 0, 0)).rgb;
    float  is_tint    = TintMaskT.Load(int3((int)idx, 0, 0));
    float3 tint_rgb   = v.col.rgb;
    float  brightness = v.col.a;
    float  alpha_scl  = AlphaTex.Load(int3(int2(v.pos.xy), 0)) * 255.0 / 127.0;

    float3 light = lerp(brightness.xxx, tint_rgb * brightness, is_tint) * alpha_scl;
    float3 lit   = base * light;

    PSOut o;
    o.color = float4(saturate(lit), 1.0);
    uint z = ZAtlas.Load(int3(px, 0));
    o.depth = saturate(v.pos.z + (float)z * ZDataDepthScale);
    return o;
}
