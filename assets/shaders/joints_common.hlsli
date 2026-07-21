//============================================================================
//  joints_common.hlsli -- shared GPU skinning helper for animated glTF models.
//
//  g_Joints is a per-draw bone-matrix palette: Renderer::DrawModel/DrawModelShadow
//  upload it fresh (MAP_WRITE + MAP_FLAG_DISCARD) before every skinned draw, sized
//  to exactly what that draw needs (see core/rendering/renderer.cpp). Vertex joint
//  indices/weights come from the loader's JOINTS_0/WEIGHTS_0 attributes, packed
//  into buffer slot 1 (Renderer::ModelVertexAttribs) -- a separate stream from
//  position/normal/uv in slot 0, matching DiligentTools' own default layout.
//============================================================================

// Diligent's own float4x4 (what Renderer::DrawModel/DrawModelShadow memcpy straight into
// this buffer) is row-major, so every shader that includes this header MUST be compiled
// with SHADER_COMPILE_FLAG_PACK_MATRIX_ROW_MAJOR -- StructuredBuffer<row_major float4x4>
// (qualifying the template argument directly) is a hard parse error on this project's
// shader compiler ("'type' : Expected"), not just a style choice, so the flag is the only
// way to get this buffer's matrices read correctly. toon_common.hlsli's own Constants
// cbuffer matrices are already explicitly row_major, so the flag changes nothing for them
// -- it exists here purely for g_Joints (confirmed against every skinned .hlsl file's own
// CreateToonShader call in renderer.cpp).
StructuredBuffer<float4x4> g_Joints;

// Linear-blend (4-influence) skinning: the weighted sum of each influencing joint's
// current matrix. `jointOffset` selects which half of g_Joints to read when it holds
// two frames back to back (current + previous, for motion vectors); pass 0 when it
// holds only one frame's worth (the shadow pass, which has no motion vectors).
float4x4 SampleSkin(uint4 joints, float4 weights, uint jointOffset)
{
    return weights.x * g_Joints[jointOffset + joints.x]
         + weights.y * g_Joints[jointOffset + joints.y]
         + weights.z * g_Joints[jointOffset + joints.z]
         + weights.w * g_Joints[jointOffset + joints.w];
}
