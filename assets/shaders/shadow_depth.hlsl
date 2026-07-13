//============================================================================
//  shadow_depth.hlsl — cascaded shadow map depth-only pass (procedural meshes).
//
//  Position-only vertex transform into one cascade's light-space clip position; no
//  pixel shader (depth-only PSO). Declares the full toon::Vertex layout (matching
//  toon_fill.hlsl's VSInput) purely so the PSO's input layout gets the correct
//  per-vertex stride -- only Pos is actually read.
//============================================================================
cbuffer ShadowConstants
{
    row_major float4x4 g_LightWorldViewProj; // object -> this cascade's light clip space
};

struct VSInput
{
    float3 Pos          : ATTRIB0;
    float3 Normal       : ATTRIB1;  // unused; kept so the input layout's stride is correct
    float3 SmoothNormal : ATTRIB2;  // unused; kept so the input layout's stride is correct
};

float4 VSMain(VSInput vin) : SV_POSITION
{
    return mul(float4(vin.Pos, 1.0), g_LightWorldViewProj);
}
