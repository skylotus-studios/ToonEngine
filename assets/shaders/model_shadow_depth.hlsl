//============================================================================
//  model_shadow_depth.hlsl — cascaded shadow map depth-only pass (glTF models).
//
//  Same idea as shadow_depth.hlsl, but matching the glTF loader's pos/normal/uv
//  vertex layout so the PSO's input-layout stride is correct -- only Pos is read.
//============================================================================
cbuffer ShadowConstants
{
    row_major float4x4 g_LightWorldViewProj; // object -> this cascade's light clip space
};

struct VSInput
{
    float3 Pos    : ATTRIB0;
    float3 Normal : ATTRIB1;  // unused; kept so the input layout's stride is correct
    float2 UV     : ATTRIB2;  // unused; kept so the input layout's stride is correct
};

float4 VSMain(VSInput vin) : SV_POSITION
{
    return mul(float4(vin.Pos, 1.0), g_LightWorldViewProj);
}
