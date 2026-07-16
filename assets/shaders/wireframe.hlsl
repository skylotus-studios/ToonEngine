//============================================================================
//  wireframe.hlsl — flat-color line-list draw (M2.1's collider debug overlay).
//
//  Self-contained: no shared toon_common.hlsli include, since this has no lighting,
//  banding, shadow, or G-buffer output — just position -> clip and a flat color.
//  Drawn directly onto the resolved back buffer (see Renderer::DrawWireframe), so it
//  always renders on top, un-occluded by scene geometry.
//============================================================================
cbuffer Constants
{
    row_major float4x4 g_WorldViewProj;
    float4 g_Color;
};

struct VSInput
{
    float3 Pos : ATTRIB0;
};

struct PSInput
{
    float4 Pos : SV_Position;
};

PSInput VSMain(VSInput vin)
{
    PSInput o;
    o.Pos = mul(float4(vin.Pos, 1.0), g_WorldViewProj);
    return o;
}

float4 PSMain(PSInput pin) : SV_Target
{
    return g_Color;
}
