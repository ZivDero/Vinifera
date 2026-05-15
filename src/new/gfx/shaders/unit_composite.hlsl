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
    // Skip transparent scratch pixels so the depth test only sees the
    // unit's actual footprint.
    if (c.a <= 0.0) discard;
    PSOut o;
    // Scratch is premultiplied; scale by unit alpha for the final
    // `(scratch*unit_a) + (1 - scratch.a*unit_a)*scene` scene blend.
    o.color = c * Alpha;
    // Per-unit constant depth from `Render_Deferred_Composite`, anchored
    // at the unit's drawpoint Y to match `Tile_Base_Depth_From_Visual_Y`.
    o.depth = v.pos.z;
    return o;
}
