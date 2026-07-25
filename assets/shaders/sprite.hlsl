//============================================================================
//  sprite.hlsl — transparent, unlit, textured quad (roadmap #13: 2D & sprites).
//
//  Self-contained (no shared toon_common.hlsli include), same "flat draw, no lighting/
//  banding" shape as wireframe.hlsl/sky_gradient.hlsl. Drawn into the HDR G-buffer after
//  the opaque toon pass, before EndScene (see Renderer::DrawSprite): depth-tested against
//  opaque geometry, alpha-blended on the color target only.
//
//  Writes REAL data to all three targets, not just color. The first version write-masked
//  the normal/motion targets off, which left the OCCLUDED object's normals/roughness/motion
//  under every sprite pixel -- so the screen-space effects (SSR/SSAO/TAA, which read those
//  buffers at resolve time) composited the hidden object's lighting response ON TOP of the
//  sprite: most visibly, SSR painted reflections of the scene wherever a sprite covered the
//  reflective ground, reading as "I can see objects through the sprite." Now: the quad's own
//  facing normal with roughness pinned to 1 in .w (fully matte -- SSR always skips sprite
//  pixels), and the sprite's own motion vector (so TAA/temporal reprojection tracks the
//  sprite, not whatever it covers).
//============================================================================
cbuffer Constants
{
    row_major float4x4 g_WorldViewProj;     // object -> clip (this frame)
    row_major float4x4 g_PrevWorldViewProj; // object -> clip (previous frame), for motion vectors
    row_major float4x4 g_World;             // object -> world (for the quad's world-space normal)
    float4 g_Tint;
    float4 g_UVRect; // xy = offset, zw = scale into the source texture (atlas sub-rect)
};

// _sampler suffix is Diligent's combined-texture-sampler convention (see model_fill.hlsl's
// g_Albedo/g_Albedo_sampler); bound DYNAMIC on one shared SRB, re-Set per draw
// (Renderer::DrawSprite), with an immutable linear-clamp sampler attached at PSO creation.
Texture2D    g_SpriteTex;
SamplerState g_SpriteTex_sampler;

struct VSInput
{
    float3 Pos : ATTRIB0;
    float2 UV  : ATTRIB1;
};

struct VSOut
{
    float4 Pos         : SV_POSITION;
    float2 UV          : TEXCOORD0;
    float3 WorldNormal : TEXCOORD1; // the quad's geometric normal (constant across the quad)
    float4 CurrClip    : TEXCOORD2; // clip-space pos this frame (for motion vectors)
    float4 PrevClip    : TEXCOORD3; // clip-space pos last frame
};

struct PSOut
{
    float4 Color  : SV_Target0; // alpha-blended sprite color
    float4 Normal : SV_Target1; // world-space normal (xyz); w = roughness, pinned 1 = matte (no SSR)
    float2 Motion : SV_Target2; // NDC-space (currNDC - prevNDC), toon_common's ComputeMotion convention
};

VSOut VSMain(VSInput vin)
{
    VSOut o;
    o.Pos = mul(float4(vin.Pos, 1.0), g_WorldViewProj);
    o.CurrClip = o.Pos;
    o.PrevClip = mul(float4(vin.Pos, 1.0), g_PrevWorldViewProj);
    o.UV = vin.UV;
    // The quad lies in its object-space XY plane, so its world normal is the cross product
    // of the world matrix's transformed X and Y basis vectors (rows 0/1, row-vector
    // convention) -- correct under any scale, including non-uniform in-plane scale, where
    // transforming local +Z by g_World directly would not be.
    o.WorldNormal = normalize(cross(g_World[0].xyz, g_World[1].xyz));
    return o;
}

PSOut PSMain(VSOut i, bool frontFace : SV_IsFrontFace)
{
    // Atlas sub-rect remap (flip X/Y is folded into g_UVRect's offset/scale by the caller,
    // ToonEngineOld's convention: negate the axis's scale and shift the offset, rather than
    // branching here).
    float2 uv = i.UV * g_UVRect.zw + g_UVRect.xy;
    float4 col = g_SpriteTex.Sample(g_SpriteTex_sampler, uv) * g_Tint;
    if (col.a < 0.01) discard;

    // Face the viewer: the quad is CULL_NONE (visible from both sides), so flip the geometric
    // normal toward the camera. The quad is authored CCW-from-+Z while the PSO leaves the
    // default clockwise-is-front winding, so the +Z side rasterizes as a BACK face:
    // frontFace == false means the camera is on the normal's own side. (If this sign ever
    // proves inverted on another backend, the failure mode is subtle SSAO shading on
    // sprites, not see-through -- roughness = 1 below gates SSR regardless of the normal.)
    float3 n = frontFace ? -i.WorldNormal : i.WorldNormal;

    // Same math as toon_common.hlsli's ComputeMotion (this file deliberately has no include).
    float2 currNDC = i.CurrClip.xy / i.CurrClip.w;
    float2 prevNDC = i.PrevClip.xy / i.PrevClip.w;

    PSOut o;
    o.Color = col;
    o.Normal = float4(n, 1.0); // w = roughness 1: above SSR's RoughnessThreshold (0.2 default),
                               // so its stencil mask excludes sprite pixels entirely
    o.Motion = currNDC - prevNDC;
    return o;
}
