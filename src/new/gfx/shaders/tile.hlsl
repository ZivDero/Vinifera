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
 *  Vanilla lighting model — replicated in float per pixel.
 *
 *  Per pixel, vanilla does:
 *    row = AlphaLightingRemap[cell_color_q][alpha_byte] >> 8
 *    out = drawer.Translator[row * 256 + idx]
 *
 *  Where drawer.Translator row N is built by `Apply_Tint`:
 *    if TintMask[idx]: rgb = palette[idx].rgb * tint_rgb * (2N/62)
 *    else:             rgb = palette[idx].rgb * clamp(N/max_level, 0, 1)
 *  with max_level ≈ 8 (= 30*63/200 - 1).
 *
 *  Inputs from the vertex stream:
 *    v.col.rgb = cell.RedTint/GreenTint/BlueTint   (already / 1000)
 *    v.col.a   = cell.TileBrightness               (already / 1000)
 */
PSOut PSMain(VSOut v)
{
    int2 px = int2(v.uv * AtlasSize);
    uint idx = Atlas.Load(int3(px, 0));
    if (idx == 0) discard;

    float3 base    = Palette.Load(int3((int)idx, 0, 0)).rgb;
    float  is_tint = TintMaskT.Load(int3((int)idx, 0, 0));
    float3 tint_rgb   = v.col.rgb;
    float  cell_color = v.col.a * 1000.0;

    /* AlphaLightingRemap quantisation. */
    float cc_q = clamp(floor((261.0 * cell_color) / 2048.0), 0.0, 254.0);
    float alpha_b = AlphaTex.Load(int3(int2(v.pos.xy), 0)) * 255.0;
    const float kLevels = 62.0;
    float row = clamp(floor((alpha_b * cc_q * kLevels) / 32258.0), 0.0, kLevels);

    /* Tinted path: linear 0..2.0 across the 62 levels. */
    float tint_intensity = row / 31.0;
    /* Untinted path: ramp 0..1 across the first ~8 levels, then plateau. */
    const float kMaxLevel = 8.0;
    float intensity = saturate(row / kMaxLevel);

    float3 lit = base * lerp(intensity.xxx, tint_rgb * tint_intensity, is_tint);
    PSOut o;
    o.color = float4(saturate(lit), 1.0);
    uint z = ZAtlas.Load(int3(px, 0));
    o.depth = saturate(v.pos.z + (float)z * ZDataDepthScale);
    return o;
}
