cbuffer PrimitiveCB : register(b0) { float4x4 ProjMtx; };

struct VSIn  { float2 pos : POSITION; float4 col : COLOR0; };
struct VSOut { float4 pos : SV_Position; float4 col : COLOR0; };

VSOut VSMain(VSIn i)
{
    VSOut o;
    o.pos = mul(ProjMtx, float4(i.pos.xy, 0.0f, 1.0f));
    o.col = i.col;
    return o;
}

float4 PSMain(VSOut v) : SV_Target { return v.col; }
