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
    o.WorldNormal = mul(float4(vin.Normal, 0.0), g_NormalMatrix).xyz;  // inverse-transpose: correct under non-uniform scale
    o.CurrClip    = o.Pos;
    o.PrevClip    = mul(float4(vin.Pos, 1.0), g_PrevWorldViewProj);
    return o;
}

PSOutput PSMain(PSInput pin)
{
    float3 N = normalize(pin.WorldNormal);

    PSOutput o;
    // Banded (cel) diffuse; g_Params.x = bands, .y = ambient floor. g_LightColor tints/scales
    // the whole ramp (including the ambient floor) by the light's color * intensity.
    o.Color  = float4(CelShade(g_BaseColor.rgb, N, g_LightDir.xyz, g_Params.x, g_Params.y) * g_LightColor.rgb, 1.0);
    o.Normal = float4(N, g_Params.z);   // world normal (SSAO) + roughness in w (SSR)
    o.Motion = ComputeMotion(pin.CurrClip, pin.PrevClip);
    return o;
}
