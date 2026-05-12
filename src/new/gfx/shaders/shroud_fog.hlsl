/**
 *  Vanilla shroud/fog formulas from `CellClass::Draw_Shroud_Or_Fog_Shape`
 *  and `CellClass::Draw_Fog_Shape`.
 */

cbuffer SpriteCB : register(b0)
{
    float4x4 ProjMtx;
};
cbuffer EffectCB : register(b1)
{
    float2 AtlasSize;
    uint   Mode;       /* 0 = ShroudOverwrite, 1 = FogAdditive */
    uint   _pad;
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

void PSMain(VSOut v)
{
    int2 px = int2(v.uv * AtlasSize);
    uint shape = Atlas.Load(int3(px, 0));
    int2 dst = int2(v.pos.xy);
    if (Mode == 0) {
        /* ShroudOverwrite — CellClass::Draw_Shroud_Or_Fog_Shape. */
        if (shape == 0xFE) discard;
        AlphaUAV[dst] = (float)shape / 255.0;
    } else {
        /* FogAdditive — CellClass::Draw_Fog_Shape. */
        if (shape > 0x7F) discard;
        float old_byte = AlphaUAV[dst] * 255.0;
        float new_byte;
        if (abs(old_byte - 127.0) < 0.5) {
            new_byte = (float)shape;
        } else {
            new_byte = max(0.0, old_byte + (float)shape - 127.0);
        }
        AlphaUAV[dst] = new_byte / 255.0;
    }
}
