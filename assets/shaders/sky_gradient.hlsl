//============================================================================
//  sky_gradient.hlsl — editor backdrop: two-color vertical sky gradient.
//
//  Self-contained (no shared toon_common.hlsli include), same "flat draw, no
//  lighting/banding" shape as wireframe.hlsl. Drawn as a single full-screen
//  triangle straight into the HDR G-buffer, before scene geometry (see
//  Renderer::DrawSky) -- the ground grid itself is DiligentFX's own
//  CoordinateGridRenderer, not this file (see Renderer::DrawGrid).
//============================================================================
cbuffer SkyConstants
{
    row_major float4x4 g_InvViewProj;
    float4 g_SkyTop;
    float4 g_SkyBottom;
};

struct VSOut
{
    float4 Pos : SV_POSITION;
    float2 NDC : TEXCOORD0;
};

struct PSOut
{
    float4 Color  : SV_Target0; // HDR scene color
    float4 Normal : SV_Target1; // world-space normal G-buffer (unused by the sky)
    float2 Motion : SV_Target2; // NDC motion vectors (unused by the sky)
};

// Full-screen triangle straight from the vertex id (tonemap.hlsl's own idiom) -- covers the
// screen with one oversized triangle, no vertex/index buffer required. NDC is passed through
// (not just a [0,1] UV) so PSMain can reconstruct a world-space ray per pixel.
VSOut VSMain(uint id : SV_VertexID)
{
    VSOut o;
    float2 uv = float2((id << 1) & 2, id & 2);   // (0,0) (2,0) (0,2)
    float2 ndc = uv * float2(2.0, -2.0) + float2(-1.0, 1.0);
    o.NDC = ndc;
    o.Pos = float4(ndc, 0.0, 1.0);
    return o;
}

PSOut PSMain(VSOut i)
{
    // World-space view ray for this pixel: unproject the near/far NDC points and take their
    // difference. Diligent/Vulkan depth range is [0,1] (near = z 0, far = z 1), same
    // convention Renderer::ScreenPointToRay uses. Camera position cancels out of the
    // difference, so only direction -- not camera position -- is needed for the gradient.
    float4 nearP = mul(float4(i.NDC, 0.0, 1.0), g_InvViewProj);
    float4 farP  = mul(float4(i.NDC, 1.0, 1.0), g_InvViewProj);
    nearP /= nearP.w;
    farP  /= farP.w;
    float3 rayDir = normalize(farP.xyz - nearP.xyz);

    // Horizon-anchored gradient: lerp by the ray's world-space Y direction, not screen Y, so
    // the horizon stays level as the camera pitches (ToonEngineOld's grid.frag sky).
    float t = saturate(rayDir.y * 0.5 + 0.5);

    PSOut o;
    o.Color = float4(lerp(g_SkyBottom.rgb, g_SkyTop.rgb, t), 1.0);
    o.Normal = float4(0.0, 0.0, 0.0, 0.0);
    o.Motion = float2(0.0, 0.0);
    return o;
}
