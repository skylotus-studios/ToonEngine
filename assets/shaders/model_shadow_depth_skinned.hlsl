//============================================================================
//  model_shadow_depth_skinned.hlsl — cascaded shadow map depth-only pass for
//  animated (skinned) glTF models.
//
//  Same idea as model_shadow_depth.hlsl, but skins position through the current
//  bone pose first -- so an animated character's shadow matches its animated
//  silhouette instead of a static bind-pose shadow. No motion vectors in a
//  depth-only pass, so only the current pose is needed (unlike the fill/outline
//  skinned shaders, which also skin a previous pose).
//============================================================================
#include "joints_common.hlsli"

cbuffer ShadowConstants
{
    row_major float4x4 g_LightWorldViewProj; // object -> this cascade's light clip space
};

struct VSInput
{
    float3 Pos     : ATTRIB0;
    float3 Normal  : ATTRIB1; // unused; kept so the input layout's stride is correct
    float2 UV      : ATTRIB2; // unused; kept so the input layout's stride is correct
    float4 Joints  : ATTRIB3;
    float4 Weights : ATTRIB4;
};

float4 VSMain(VSInput vin) : SV_POSITION
{
    const float4x4 skin = SampleSkin(uint4(vin.Joints), vin.Weights, 0);
    const float3 skinnedPos = mul(float4(vin.Pos, 1.0), skin).xyz;
    return mul(float4(skinnedPos, 1.0), g_LightWorldViewProj);
}
