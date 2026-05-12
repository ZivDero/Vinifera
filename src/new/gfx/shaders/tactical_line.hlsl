cbuffer SpriteCB : register(b0)
{
    float4x4 ProjMtx;
};
cbuffer TacticalLineCB : register(b1)
{
    float4 ColorStart;
    float4 ColorEnd;
    float  ZStart;
    float  ZEnd;
    uint   Flags;
    uint   _pad;
};
static const uint TLF_DEPTH_TEST     = 0x01;
static const uint TLF_DEPTH_WRITE    = 0x02;
static const uint TLF_ALPHA_MOD      = 0x04;
static const uint TLF_ALPHA_TEST_BG  = 0x08;
static const uint TLF_ALPHA_TEST_FG  = 0x10;
static const uint TLF_GRADIENT       = 0x20;

struct VSIn  { float2 pos : POSITION; float t : TEXCOORD0; };
struct VSOut { float4 pos : SV_Position; float t : TEXCOORD0; };

VSOut VSMain(VSIn i)
{
    VSOut o;
    o.pos = mul(ProjMtx, float4(i.pos, 0, 1));
    o.t = i.t;
    return o;
}

Texture2D<float> AlphaTex : register(t1);

/**
 * Hardware depth-test note: we DO NOT manually Load the scene
 * depth SRV here. The same depth resource is bound writable as
 * the active DSV (see GraphicsDevice::Bind_Scene_Target). D3D11
 * silently unbinds an SRV that aliases a bound writable DSV, so
 * any manual Load() in this shader reads 0 and discards every
 * pixel. Depth testing is done through SV_Depth + the bound
 * EDepthStencil state (TestLessEqual_NoWrite or WriteLessEqual)
 * — exactly equivalent to what the manual test would do.
 */
struct PSOut { float4 color : SV_Target; float depth : SV_Depth; };

PSOut PSMain(VSOut v)
{
    PSOut o;
    float interp_z = lerp(ZStart, ZEnd, v.t);
    o.depth = interp_z;
    /* Alpha-buffer interactions all share one sample. */
    uint alpha_mask = Flags & (TLF_ALPHA_MOD | TLF_ALPHA_TEST_BG | TLF_ALPHA_TEST_FG);
    float alpha_byte = 127.0;
    if (alpha_mask) {
        alpha_byte = AlphaTex.Load(int3(int2(v.pos.xy), 0)) * 255.0;
        if ((Flags & TLF_ALPHA_TEST_BG) && alpha_byte != 0.0) discard;
        if ((Flags & TLF_ALPHA_TEST_FG) && alpha_byte == 0.0) discard;
    }
    float4 col = (Flags & TLF_GRADIENT) ? lerp(ColorStart, ColorEnd, v.t) : ColorStart;
    if (Flags & TLF_ALPHA_MOD) {
        col.rgb *= alpha_byte / 127.0;
    }
    col.rgb *= col.a;   /* premultiply for the bound blend state */
    o.color = col;
    return o;
}
