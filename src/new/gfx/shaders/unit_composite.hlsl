cbuffer SpriteCB : register(b0)
{
    float4x4 ProjMtx;
};
cbuffer EffectCB : register(b1)
{
    float  Alpha;
    float3 _Pad;
};

struct VSIn {
    float3 pos    : POSITION;
    float2 uv     : TEXCOORD0;
    float2 zuv    : TEXCOORD1;
    float4 col    : COLOR0;
    uint   layer  : TEXCOORD2;
    uint   pflags : TEXCOORD3;
};
struct VSOut {
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

VSOut VSMain(VSIn i)
{
    VSOut o;
    float4 p = mul(ProjMtx, float4(i.pos.xy, 0, 1));
    o.pos = float4(p.x, p.y, i.pos.z, 1);
    o.uv  = i.uv;
    return o;
}

Texture2D<float4> Scratch : register(t0);
SamplerState      PointS  : register(s0);

struct PSOut {
    float4 color : SV_Target;
    float  depth : SV_Depth;
};

PSOut PSMain(VSOut v)
{
    float4 c = Scratch.Sample(PointS, v.uv);
    // Discard fully-transparent scratch pixels so we don't trip
    // the depth test on empty unit area outside the unit's actual
    // voxel footprint.
    if (c.a <= 0.0) discard;
    PSOut o;
    // Scratch is already premultiplied. Scale by unit alpha so
    // the final scene blend is `(scratch*unit_a) + (1 - scratch.a*unit_a)*scene`.
    o.color = c * Alpha;
    // Per-pixel SV_Depth: emit a depth value that just barely beats
    // terrain at THIS pixel's screen-Y. Terrain depth at pixel Y is
    // `1 - Y * kPixelToDepth` (1/16000); subtract a small eps so
    // composite consistently wins LessEqual against terrain across
    // the whole 256x256 unit footprint. Without this, a single
    // per-unit depth value misses on roughly half the unit (where
    // terrain happens to be closer than our chosen baseline).
    const float kPixelToDepth = 1.0 / 16000.0;
    o.depth = clamp(1.0 - v.pos.y * kPixelToDepth - 1.0e-4, 1.0e-4, 0.9999);
    return o;
}
