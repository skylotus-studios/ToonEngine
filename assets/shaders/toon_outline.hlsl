//============================================================================
//  toon_outline.hlsl — inverted-hull outline pass.
//
//  Each vertex is pushed out along its normal (object space); the outline PSO
//  culls FRONT faces, so only the enlarged back-facing shell survives. Drawn
//  before the fill, it shows as a solid-color rim around the silhouette.
//============================================================================
#include "toon_common.hlsli"

PSInput VSMain(VSInput vin)
{
    PSInput o;
    // Extrude along the SMOOTH normal so hard edges (e.g. a cube's corners) stay
    // closed — the per-face normal would split shared verts apart and gap.
    //
    // The extrude is g_Outline.w *world-space* units so it stays uniform under
    // non-uniform scale (a plain object-space push would be stretched by the scale).
    // We still offset the object-space position and reuse the existing WVP path: take
    // the true world normal (g_NormalMatrix), then bring a world-space step back into
    // object space through world^-1 (the 3x3 transpose of g_NormalMatrix). For uniform
    // scale this reduces exactly to `vin.Pos + vin.SmoothNormal * g_Outline.w`.
    float3   worldNormal = normalize(mul(float4(vin.SmoothNormal, 0.0), g_NormalMatrix).xyz);
    float3x3 worldInv    = transpose((float3x3)g_NormalMatrix);            // world^-1 (3x3)
    float3   inflated    = vin.Pos + mul(worldNormal, worldInv) * g_Outline.w;
    o.Pos           = mul(float4(inflated, 1.0), g_WorldViewProj);
    o.WorldNormal   = mul(float4(vin.Normal, 0.0), g_NormalMatrix).xyz;    // world space, for the G-buffer
    o.CurrClip      = o.Pos;
    o.PrevClip      = mul(float4(inflated, 1.0), g_PrevWorldViewProj);
    return o;
}

PSOutput PSMain(PSInput pin)
{
    // The rim is mostly overwritten by the fill; where it survives (the silhouette)
    // still needs a sane G-buffer normal so SSAO doesn't read garbage there.
    PSOutput o;
    o.Color  = float4(g_Outline.rgb, 1.0);
    o.Normal = float4(normalize(pin.WorldNormal), g_Params.z);   // + roughness in w (SSR)
    o.Motion = ComputeMotion(pin.CurrClip, pin.PrevClip);
    return o;
}
