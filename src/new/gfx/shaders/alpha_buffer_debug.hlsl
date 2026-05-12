cbuffer AlphaDebugCB : register(b0)
{
    float Invert;
    float Pad0;
    float Pad1;
    float Pad2;
};

struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };

VSOut VSMain(uint id : SV_VertexID)
{
    VSOut o;
    float2 uv = float2((id << 1) & 2, id & 2);
    o.pos = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
    o.uv = uv;
    return o;
}

Texture2D<float> AlphaTex : register(t0);
SamplerState     Smp      : register(s0);

float4 PSMain(VSOut v) : SV_Target
{
    float a = AlphaTex.SampleLevel(Smp, v.uv, 0);
    if (Invert > 0.5) a = 1.0 - a;
    return float4(a, a, a, 1.0);
}
