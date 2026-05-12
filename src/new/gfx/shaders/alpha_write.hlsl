cbuffer SpriteCB : register(b0)
{
    float4x4 ProjMtx;
};
cbuffer EffectCB : register(b1)
{
    float2 AtlasSize;
    uint2  _pad;
};

Texture2D<uint>          Atlas    : register(t0);
RWTexture2D<unorm float> AlphaUAV : register(u0);

struct VSIn  { float3 pos : POSITION; float2 uv : TEXCOORD0; float2 zuv : TEXCOORD1; float4 col : COLOR0; };
struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };

VSOut VSMain(VSIn i)
{
    VSOut o;
    float4 p = mul(ProjMtx, float4(i.pos.xy, 0, 1));
    o.pos = float4(p.x, p.y, 0, 1);
    o.uv  = i.uv;
    return o;
}

/**
 * Vanilla `AlphaShapeClass::BrightnessTable[shape][old]` is
 *   out_byte = clamp(shape * old / 127, 0, 255).
 * Both shape and old are 0..255 byte values. We read `old` from
 * the UNORM UAV (already 0..1), reconstruct the byte, apply the
 * formula, and write back the saturating result.
 */
void PSMain(VSOut v)
{
    int2 px = int2(v.uv * AtlasSize);
    uint shape_byte = Atlas.Load(int3(px, 0));
    if (shape_byte == 0) discard;

    int2 dst = int2(v.pos.xy);
    float old = AlphaUAV[dst];
    float new_val = saturate(shape_byte * old / 127.0);
    AlphaUAV[dst] = new_val;
}
