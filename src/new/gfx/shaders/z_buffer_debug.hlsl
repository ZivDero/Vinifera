cbuffer ZDebugCB : register(b0)
{
    float MinDepth;
    float MaxDepth;
    float Invert;
    float Pad;
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

Texture2D<float> DepthTex : register(t0);
SamplerState     Smp      : register(s0);

float4 PSMain(VSOut v) : SV_Target
{
    float depth = DepthTex.SampleLevel(Smp, v.uv, 0);
    float denom = max(MaxDepth - MinDepth, 0.000001);
    float value = saturate((depth - MinDepth) / denom);
    if (Invert > 0.5) value = 1.0 - value;
    return float4(value, value, value, 1.0);
}
