//============================================================================
//  toon_fill.hlsl — the toon fill pass: banded (cel) diffuse lighting.
//
//  Instead of a smooth N·L gradient, the diffuse term is quantized into a small
//  number of flat bands — the characteristic cel-shaded ramp.
//============================================================================
#include "toon_common.hlsli"

PSInput VSMain(VSInput vin)
{
    PSInput o;
    o.Pos         = mul(float4(vin.Pos, 1.0), g_WorldViewProj);
    o.WorldNormal = mul(float4(vin.Normal, 0.0), g_World).xyz;
    o.CurrClip    = o.Pos;
    o.PrevClip    = mul(float4(vin.Pos, 1.0), g_PrevWorldViewProj);
    return o;
}

PSOutput PSMain(PSInput pin)
{
    float3 N     = normalize(pin.WorldNormal);
    float3 L     = normalize(g_LightDir.xyz);
    float  NdotL = saturate(dot(N, L));

    // Quantize the diffuse term into `bands` distinct levels spanning [0, 1].
    float bands = max(g_Params.x, 1.0);
    float ramp  = saturate(floor(NdotL * bands) / max(bands - 1.0, 1.0));

    // Ambient floor keeps the shadow side from crushing to pure black.
    float ambient = g_Params.y;
    float shade   = lerp(ambient, 1.0, ramp);

    PSOutput o;
    o.Color  = float4(g_BaseColor.rgb * shade, 1.0);
    o.Normal = float4(N, 0.0);   // world-space normal for the SSAO G-buffer
    o.Motion = ComputeMotion(pin.CurrClip, pin.PrevClip);
    return o;
}
