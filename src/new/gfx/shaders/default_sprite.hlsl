cbuffer SpriteCB : register(b0) { float4x4 ProjMtx; };

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

Texture2D    Tex : register(t0);
SamplerState Smp : register(s0);

float4 PSMain(VSOut v) : SV_Target { return Tex.Sample(Smp, v.uv) * v.col; }
