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
    float3 inflated = vin.Pos + vin.SmoothNormal * g_Outline.w;  // object-space extrude
    o.Pos           = mul(float4(inflated, 1.0), g_WorldViewProj);
    o.WorldNormal   = vin.Normal;
    return o;
}

float4 PSMain(PSInput pin) : SV_TARGET
{
    return float4(g_Outline.rgb, 1.0);
}
