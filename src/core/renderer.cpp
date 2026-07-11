//============================================================================
//  core/renderer.cpp — Diligent Engine (Vulkan) implementation of the seam.
//
//  Everything Diligent-specific is contained in this translation unit. The
//  public header (renderer.h) exposes only opaque handles and plain types.
//============================================================================
#include "core/renderer.h"

// GLFW with native access — extracting the OS window handle is a backend
// concern, so it lives behind the seam here. main.cpp includes only plain GLFW.
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#if defined(_WIN32)
#   define GLFW_EXPOSE_NATIVE_WIN32
#elif defined(__linux__)
#   define GLFW_EXPOSE_NATIVE_X11
#endif
#if defined(_WIN32) || defined(__linux__)
#   include <GLFW/glfw3native.h>
#endif

#include "EngineFactoryVk.h"
#include "RenderDevice.h"
#include "DeviceContext.h"
#include "SwapChain.h"
#include "GraphicsTypes.h"
#include "Buffer.h"
#include "Texture.h"
#include "Sampler.h"
#include "Shader.h"
#include "PipelineState.h"
#include "RefCntAutoPtr.hpp"
#include "MapHelper.hpp"     // Diligent-GraphicsTools
#include "BasicMath.hpp"

// DiligentFX post-processing: the Bloom + SSAO effects and the shared PostFXContext
// they depend on. All compile into the DiligentFX target, whose root is on the
// include path, so these resolve short-form.
#include "PostFXContext.hpp"
#include "Bloom.hpp"
#include "ScreenSpaceAmbientOcclusion.hpp"
#include "DepthOfField.hpp"
#include "TemporalAntiAliasing.hpp"
#include "ScreenSpaceReflection.hpp"

// DiligentTools asset loading: the glTF/GLB loader (Diligent::GLTF::Model owns the
// vertex/index buffers + textures) and the texture-from-file helper. Self-contained in
// DiligentTools — no DiligentFX / PBR renderer needed (we cel-shade with our own PSO).
#include "GLTFLoader.hpp"
#include "TextureUtilities.h"

// Dear ImGui + Diligent's ImGui renderer backend (DiligentTools). The GLFW
// platform backend (imgui_impl_glfw.cpp) is compiled directly into ToonEngine
// by CMakeLists.txt, since DiligentTools doesn't ship a GLFW backend itself.
#include "imgui.h"
#include "ImGuiImplDiligent.hpp"
#include "backends/imgui_impl_glfw.h"

#include <cstdio>
#include <memory>
#include <vector>

// Absolute path to the HLSL sources, baked in by CMake so the shader stream
// factory finds them regardless of working directory (dev convenience; a
// shipped build would copy shaders next to the exe instead).
#ifndef TOON_SHADERS_DIR
#   define TOON_SHADERS_DIR "assets/shaders"
#endif

using namespace Diligent;

// DiligentFX's effect-parameter structs, compiled as C++ from the same headers its
// shaders include. BasicStructures.fxh brings in CameraAttribs (which PostFXContext
// wants); BloomStructures.fxh defines BloomAttribs. float4x4/float4/uint etc.
// resolve to the Diligent:: aliases pulled in by BasicMath.hpp above. (Mirrors how
// DiligentFX's own .cpp files include these — see Bloom.cpp.) BasicStructures.fxh
// does a bare #include "ShaderDefinitions.fxh"; CMake puts that directory on the
// include path so it resolves.
namespace Diligent { namespace HLSL {
#include "Shaders/Common/public/BasicStructures.fxh"
#include "Shaders/PostProcess/Bloom/public/BloomStructures.fxh"
#include "Shaders/PostProcess/ScreenSpaceAmbientOcclusion/public/ScreenSpaceAmbientOcclusionStructures.fxh"
#include "Shaders/PostProcess/DepthOfField/public/DepthOfFieldStructures.fxh"
#include "Shaders/PostProcess/TemporalAntiAliasing/public/TemporalAntiAliasingStructures.fxh"
#include "Shaders/PostProcess/ScreenSpaceReflection/public/ScreenSpaceReflectionStructures.fxh"
}} // namespace Diligent::HLSL

namespace toon {

// --- Internal formats, GPU-mirror types & PIMPL state -----------------------

// The scene renders to an HDR color target + a world-space normal G-buffer (for
// SSAO), then a full-screen pass tone-maps to the back buffer. RGBA16F holds signed
// normals in [-1,1] directly, so no encode/decode is needed.
static constexpr TEXTURE_FORMAT kHDRFormat        = TEX_FORMAT_RGBA16_FLOAT;
static constexpr TEXTURE_FORMAT kNormalFormat     = TEX_FORMAT_RGBA16_FLOAT;
static constexpr TEXTURE_FORMAT kMotionFormat     = TEX_FORMAT_RG16_FLOAT;   // NDC motion (SSAO temporal/DoF)
static constexpr TEXTURE_FORMAT kSceneDepthFormat = TEX_FORMAT_D32_FLOAT;

// GPU mirror of the toon_common.hlsli cbuffer. Field order/size MUST match it
// (four row-major float4x4 rows + four float4 rows = 320 bytes, 16-aligned).
struct ShaderConstants {
    float4x4 worldViewProj;
    float4x4 world;
    float4x4 normalMatrix;        // inverse-transpose of world (correct normals under non-uniform scale)
    float4x4 prevWorldViewProj;   // previous frame, for motion vectors
    float4   lightDir;
    float4   baseColor;
    float4   outline;      // rgb color, w = extrude width
    float4   params;       // x = bands, y = ambient, z = roughness
};

// GPU mirror of tonemap.hlsl's PostConstants.
struct PostConstants {
    float exposure;
    float toneMap;       // 1 = ACES
    float outputSRGB;    // 1 = encode sRGB in-shader
    float ssaoStrength;  // 0 = AO ignored, 1 = full occlusion
    float ssrStrength;   // reflection add strength
    float pad0, pad1, pad2;
};

// All Diligent state hides here, behind the PIMPL boundary.
struct Renderer::Impl {
    RefCntAutoPtr<IRenderDevice>  device;
    RefCntAutoPtr<IDeviceContext> context;
    RefCntAutoPtr<ISwapChain>     swapChain;
    std::unique_ptr<ImGuiImplDiligent> imgui;

    // Toon pipeline.
    RefCntAutoPtr<IShaderSourceInputStreamFactory> shaderFactory;
    RefCntAutoPtr<IPipelineState>         fillPSO;
    RefCntAutoPtr<IPipelineState>         outlinePSO;
    RefCntAutoPtr<IShaderResourceBinding> fillSRB;
    RefCntAutoPtr<IShaderResourceBinding> outlineSRB;
    RefCntAutoPtr<IBuffer>                constants;

    struct GpuMesh {
        RefCntAutoPtr<IBuffer> vertexBuffer;
        RefCntAutoPtr<IBuffer> indexBuffer;
        Uint32                 indexCount = 0;
    };
    std::vector<GpuMesh> meshes;   // handle N -> meshes[N-1]; handle 0 = Invalid

    // glTF models (DiligentTools loader). Each GLTF::Model owns its GPU vertex/index
    // buffers + textures; the cel-fill PSO + SRB draw them. handle N -> models[N-1].
    RefCntAutoPtr<IPipelineState>             modelPSO;         // textured cel fill
    RefCntAutoPtr<IShaderResourceBinding>     modelSRB;
    RefCntAutoPtr<IPipelineState>             modelOutlinePSO;  // inverted-hull outline
    RefCntAutoPtr<IShaderResourceBinding>     modelOutlineSRB;
    std::vector<std::unique_ptr<GLTF::Model>> models;

    // HDR offscreen scene target + tone-map resolve to the back buffer.
    RefCntAutoPtr<ITexture>               hdrColor;      // RGBA16F scene color
    RefCntAutoPtr<ITexture>               normalBuffer;  // RGBA16F world-space normals (SSAO G-buffer)
    RefCntAutoPtr<ITexture>               sceneDepth;    // D32 depth (also SRV for PostFX)
    RefCntAutoPtr<IPipelineState>         tonemapPSO;
    RefCntAutoPtr<IShaderResourceBinding> tonemapSRB;
    RefCntAutoPtr<IBuffer>                postConstants;
    bool                                  outputSRGB = false;  // back buffer is a non-sRGB UNORM

    // DiligentFX post effects (Bloom + SSAO) share a PostFXContext. It requires
    // depth + motion + camera to reach its "PSOs ready" gate (which both effects
    // check). Bloom ignores those inputs; SSAO reads real depth + a world-space
    // normal G-buffer + camera. So when a post effect runs we fill `postCamera`
    // from the actual view/proj (not zeros) and the scene writes `normalBuffer`.
    // Motion stays a zero texture (we keep no frame history), which is why SSAO's
    // temporal accumulation is off by default — it would ghost the moving scene.
    std::unique_ptr<PostFXContext>               postFX;
    std::unique_ptr<Bloom>                       bloom;
    std::unique_ptr<ScreenSpaceAmbientOcclusion> ssao;
    std::unique_ptr<DepthOfField>                dof;
    std::unique_ptr<TemporalAntiAliasing>        taa;
    std::unique_ptr<ScreenSpaceReflection>       ssr;
    float2                         frameJitter{0.0f, 0.0f};  // sub-pixel proj jitter this frame (TAA)
    RefCntAutoPtr<ITexture>        motionVectors;   // RG16F NDC velocity (scene-written)
    RefCntAutoPtr<ITexture>        aoWhite;         // 1x1 white = "fully visible" default
    RefCntAutoPtr<ITexture>        ssrBlack;        // 1x1 black = "no reflection" default
    RefCntAutoPtr<ITexture>        modelWhite;      // 1x1 white 2D-ARRAY = untextured model albedo
    HLSL::CameraAttribs            postCamera{};
    Uint32                         frameIndex = 0;

    // Run the shared PostFXContext plus whichever effects are enabled. The color
    // effects chain in order scene -> TAA -> DoF -> Bloom; `colorOut` is the last
    // stage's output (or null -> resolve the raw scene). `aoOut` (SSAO visibility) and
    // `ssrOut` (reflection radiance) composite in the resolve — null -> their defaults.
    // No-op if no effect is enabled.
    void RunPostFX(const PostParams& p, ITextureView*& colorOut, ITextureView*& aoOut,
                   ITextureView*& ssrOut);

    // Fill postCamera from the stored view/proj so PostFXContext/SSAO can rebuild
    // view-space positions from depth. (Bloom doesn't use it; SSAO does.)
    void FillCameraAttribs(const SwapChainDesc& sc);

    // Per-frame scene state. view/proj are kept split (not just viewProj) because
    // the camera attribs above need each one and their inverses. prevViewProj is last
    // frame's, so DrawMesh can build each object's previous clip position for motion.
    float4x4   view         = float4x4::Identity();
    float4x4   proj         = float4x4::Identity();
    float4x4   viewProj     = float4x4::Identity();
    float4x4   prevViewProj = float4x4::Identity();
    float      nearZ        = 0.1f;
    float      farZ         = 100.0f;
    float3     lightDir = float3(0.5f, 0.8f, -0.3f);  // world-space dir TO light (shader normalizes)
    PostParams post;
};

void Renderer::Impl::FillCameraAttribs(const SwapChainDesc& sc) {
    const float4x4 viewInv = view.Inverse();
    const float    W = static_cast<float>(sc.Width);
    const float    H = static_cast<float>(sc.Height);

    postCamera.f4ViewportSize = float4(W, H, W > 0.0f ? 1.0f / W : 0.0f, H > 0.0f ? 1.0f / H : 0.0f);
    postCamera.SetClipPlanes(nearZ, farZ);   // near < far -> non-reversed [0,1] depth
    postCamera.fSceneNearZ     = postCamera.fNearPlaneZ;
    postCamera.fSceneFarZ      = postCamera.fFarPlaneZ;
    postCamera.fSceneNearDepth = postCamera.fNearPlaneDepth;
    postCamera.fSceneFarDepth  = postCamera.fFarPlaneDepth;
    postCamera.fHandness       = view.Determinant() > 0.0f ? 1.0f : -1.0f;
    postCamera.uiFrameIndex    = frameIndex;
    postCamera.mView           = view;
    postCamera.mProj           = proj;
    postCamera.mViewProj       = viewProj;
    postCamera.mViewInv        = viewInv;
    postCamera.mProjInv        = proj.Inverse();
    postCamera.mViewProjInv    = viewProj.Inverse();
    postCamera.f4Position      = float4(float3::MakeVector(viewInv[3]), 1.0f);  // camera world pos

    // Depth-of-field lens parameters (DoF reads its circle-of-confusion from these).
    // Focal length + sensor size keep their struct defaults (50mm / 36mm).
    postCamera.fFocusDistance = post.dofFocusDist;
    postCamera.fFStop         = post.dofFStop;

    // Sub-pixel jitter used to render this frame (0 unless TAA is on). PostFX shaders
    // remove it via f2Jitter when reprojecting.
    postCamera.f2Jitter = frameJitter;
}

void Renderer::Impl::RunPostFX(const PostParams& p, ITextureView*& colorOut, ITextureView*& aoOut,
                               ITextureView*& ssrOut) {
    colorOut = nullptr;
    aoOut    = nullptr;
    ssrOut   = nullptr;
    if (!p.bloom && !p.ssao && !p.dof && !p.taa && !p.ssr) return;

    const SwapChainDesc& sc = swapChain->GetDesc();
    if (sc.Width == 0 || sc.Height == 0) return;

    // DoF uses the motion vectors (via the context) to smooth its circle-of-confusion
    // over time — safe now that motion is real.
    const auto dofFlags = DepthOfField::FEATURE_FLAG_ENABLE_TEMPORAL_SMOOTHING;
    const auto taaFlags = TemporalAntiAliasing::FEATURE_FLAG_BICUBIC_FILTER;

    // Prepare the shared context + the enabled effects (each early-outs on unchanged
    // size). The PSOs are what PostFXContext::IsPSOsReady() gates on below.
    PostFXContext::FrameDesc frame;
    frame.Index  = frameIndex;
    frame.Width  = sc.Width;
    frame.Height = sc.Height;
    postFX->PrepareResources(device, frame, PostFXContext::FEATURE_FLAG_NONE);
    if (p.bloom) bloom->PrepareResources(device, context, postFX.get(), Bloom::FEATURE_FLAG_NONE);
    if (p.ssao)  ssao->PrepareResources(device, context, postFX.get(), ScreenSpaceAmbientOcclusion::FEATURE_FLAG_NONE);
    if (p.dof)   dof->PrepareResources(device, context, postFX.get(), dofFlags);
    if (p.taa)   taa->PrepareResources(device, context, postFX.get(), taaFlags);
    if (p.ssr)   ssr->PrepareResources(device, context, postFX.get(), ScreenSpaceReflection::FEATURE_FLAG_NONE);

    // Run the shared context: real camera + scene depth (as curr and prev) + the
    // scene's motion vectors. This computes the reprojected-depth / closest-motion /
    // blue-noise resources the effects build on and flips IsPSOsReady() true.
    FillCameraAttribs(sc);
    ITextureView* depthSRV  = sceneDepth->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
    ITextureView* motionSRV = motionVectors->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);

    PostFXContext::RenderAttributes pfx;
    pfx.pDevice             = device;
    pfx.pDeviceContext      = context;
    pfx.pCurrDepthBufferSRV = depthSRV;
    pfx.pPrevDepthBufferSRV = depthSRV;   // no history — reuse current
    pfx.pMotionVectorsSRV   = motionSRV;
    pfx.pCurrCamera         = &postCamera;
    pfx.pPrevCamera         = &postCamera;
    postFX->Execute(pfx);
    ++frameIndex;

    if (!postFX->IsPSOsReady()) return;   // still compiling — skip effects this frame

    if (p.ssao) {
        HLSL::ScreenSpaceAmbientOcclusionAttribs attribs{};
        attribs.EffectRadius      = p.ssaoRadius;
        attribs.ResetAccumulation = p.ssaoTemporal ? 0 : 1;   // 1 = current frame only (no ghosting)

        ScreenSpaceAmbientOcclusion::RenderAttributes ra;
        ra.pDevice          = device;
        ra.pDeviceContext   = context;
        ra.pPostFXContext   = postFX.get();
        ra.pDepthBufferSRV  = depthSRV;
        ra.pNormalBufferSRV = normalBuffer->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
        ra.pSSAOAttribs     = &attribs;
        ssao->Execute(ra);
        aoOut = ssao->GetAmbientOcclusionSRV();
    }

    if (p.ssr) {
        // Roughness rides in the normal buffer's .w, so it's both the normal and the
        // material input (RoughnessChannel = 3 selects .w).
        ITextureView* normalSRV = normalBuffer->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);

        HLSL::ScreenSpaceReflectionAttribs attribs{};
        attribs.RoughnessChannel      = 3;      // roughness is in normal.w
        attribs.IsRoughnessPerceptual = 1;      // we store artist roughness, not squared

        ScreenSpaceReflection::RenderAttributes ra;
        ra.pDevice            = device;
        ra.pDeviceContext     = context;
        ra.pPostFXContext     = postFX.get();
        ra.pColorBufferSRV    = hdrColor->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
        ra.pDepthBufferSRV    = depthSRV;
        ra.pNormalBufferSRV   = normalSRV;
        ra.pMaterialBufferSRV = normalSRV;      // roughness packed in .w
        ra.pMotionVectorsSRV  = motionSRV;
        ra.pSSRAttribs        = &attribs;
        ssr->Execute(ra);
        ssrOut = ssr->GetSSRRadianceSRV();
    }

    // Color chain: scene -> TAA (resolve) -> DoF (depth blur) -> Bloom (glow). Each
    // enabled stage reads the previous stage's output, so colorOut ends on the last
    // one that ran. TAA is first so DoF/Bloom process the anti-aliased image.
    ITextureView* colorSRV = hdrColor->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);

    if (p.taa) {
        HLSL::TemporalAntiAliasingAttribs attribs{};   // defaults (stability 0.9375)

        TemporalAntiAliasing::RenderAttributes tra;
        tra.pDevice         = device;
        tra.pDeviceContext  = context;
        tra.pPostFXContext  = postFX.get();
        tra.pColorBufferSRV = colorSRV;
        tra.pTAAAttribs     = &attribs;
        taa->Execute(tra);
        colorSRV = taa->GetAccumulatedFrameSRV();
        colorOut = colorSRV;
    }

    if (p.dof) {
        HLSL::DepthOfFieldAttribs attribs{};
        attribs.MaxCircleOfConfusion = p.dofMaxCoC;   // focus/aperture live in the camera attribs

        DepthOfField::RenderAttributes dra;
        dra.pDevice         = device;
        dra.pDeviceContext  = context;
        dra.pPostFXContext  = postFX.get();
        dra.pColorBufferSRV = colorSRV;
        dra.pDepthBufferSRV = depthSRV;
        dra.pDOFAttribs     = &attribs;
        dof->Execute(dra);
        colorSRV = dof->GetDepthOfFieldTextureSRV();
        colorOut = colorSRV;
    }

    if (p.bloom) {
        HLSL::BloomAttribs attribs{};
        attribs.Intensity    = p.bloomIntensity;
        attribs.Threshold    = p.bloomThreshold;
        attribs.SoftTreshold = p.bloomSoftKnee;   // (sic — DiligentFX's field spelling)
        attribs.Radius       = p.bloomRadius;

        Bloom::RenderAttributes bra;
        bra.pDevice         = device;
        bra.pDeviceContext  = context;
        bra.pPostFXContext  = postFX.get();
        bra.pColorBufferSRV = colorSRV;
        bra.pBloomAttribs   = &attribs;
        bloom->Execute(bra);
        colorSRV = bloom->GetBloomTextureSRV();
        colorOut = colorSRV;
    }
}

// --- File-local helpers -----------------------------------------------------

// Fill Diligent's NativeWindow from a GLFW window, per platform.
static NativeWindow MakeNativeWindow(GLFWwindow* wnd) {
    NativeWindow nw{};
#if defined(_WIN32)
    nw.hWnd = glfwGetWin32Window(wnd);
#elif defined(__linux__)
    nw.WindowId = static_cast<Uint32>(glfwGetX11Window(wnd));
    nw.pDisplay = glfwGetX11Display();
#elif defined(__APPLE__)
    // macOS needs the NSView of the Cocoa window (a few lines of Objective-C++;
    // lift it from Diligent's GLFWDemo Cocoa helper) before this will build.
#   error "Set nw.pNSView from GLFWDemo's Cocoa helper before building on macOS."
#endif
    return nw;
}

// Compile one HLSL shader stage from the shaders directory.
static RefCntAutoPtr<IShader> CreateToonShader(IRenderDevice* device,
                                               IShaderSourceInputStreamFactory* factory,
                                               SHADER_TYPE type, const char* name,
                                               const char* file, const char* entry) {
    ShaderCreateInfo ci;
    ci.SourceLanguage             = SHADER_SOURCE_LANGUAGE_HLSL;
    ci.Desc.ShaderType            = type;
    ci.Desc.Name                  = name;
    ci.Desc.UseCombinedTextureSamplers = true;
    ci.EntryPoint                 = entry;
    ci.FilePath                   = file;
    ci.pShaderSourceStreamFactory = factory;

    RefCntAutoPtr<IShader> shader;
    device->CreateShader(ci, &shader);
    return shader;
}

// Vertex attributes we ask the glTF loader to produce: POSITION / NORMAL / TEXCOORD_0,
// all interleaved into buffer 0. The model PSO's input layout is built from this SAME
// array (VertexAttributesToInputLayout), so the loader's buffer and the shader agree on
// ATTRIB0/1/2. (No smooth normal — models get the fill pass only, no inverted hull.)
static const GLTF::VertexAttributeDesc* ModelVertexAttribs(size_t& count) {
    static const GLTF::VertexAttributeDesc kAttribs[] = {
        { GLTF::PositionAttributeName,  0, VT_FLOAT32, 3 },
        { GLTF::NormalAttributeName,    0, VT_FLOAT32, 3 },
        { GLTF::Texcoord0AttributeName, 0, VT_FLOAT32, 2 },
    };
    count = sizeof(kAttribs) / sizeof(kAttribs[0]);
    return kAttribs;
}

// --- Construction & device / swap-chain bring-up ----------------------------

Renderer::Renderer()  : m_impl(new Impl) {}
Renderer::~Renderer() { Shutdown(); delete m_impl; m_impl = nullptr; }

bool Renderer::Init(GLFWwindow* window) {
#if ENGINE_DLL
    // Shared-library build: load the Vulkan engine DLL and fetch its factory.
    auto GetEngineFactoryVk = LoadGraphicsEngineVk();
#endif
    IEngineFactoryVk* factory = GetEngineFactoryVk();

    EngineVkCreateInfo engineCI;
    factory->CreateDeviceAndContextsVk(engineCI, &m_impl->device, &m_impl->context);
    if (!m_impl->device) {
        std::fprintf(stderr, "Renderer: failed to create Vulkan render device\n");
        return false;
    }

    SwapChainDesc scDesc;
    NativeWindow  window_ = MakeNativeWindow(window);
    factory->CreateSwapChainVk(m_impl->device, m_impl->context, scDesc, window_, &m_impl->swapChain);
    if (!m_impl->swapChain) {
        std::fprintf(stderr, "Renderer: failed to create swap chain\n");
        return false;
    }

    // Shader source loader (reads the .hlsl files, resolves #include).
    factory->CreateDefaultShaderSourceStreamFactory(TOON_SHADERS_DIR, &m_impl->shaderFactory);
    if (!m_impl->shaderFactory) {
        std::fprintf(stderr, "Renderer: failed to create shader source factory for '%s'\n", TOON_SHADERS_DIR);
        return false;
    }

    if (!CreateToonPipeline()) {
        std::fprintf(stderr, "Renderer: failed to create toon pipeline\n");
        return false;
    }

    if (!CreateModelPipeline()) {
        std::fprintf(stderr, "Renderer: failed to create model pipeline\n");
        return false;
    }

    // HDR pipeline: offscreen scene target + tone-map resolve to the back buffer.
    const SwapChainDesc& sc = m_impl->swapChain->GetDesc();
    if (!CreateOffscreenTargets(sc.Width, sc.Height)) {
        std::fprintf(stderr, "Renderer: failed to create offscreen HDR targets\n");
        return false;
    }

    if (!CreatePostPipeline()) {
        std::fprintf(stderr, "Renderer: failed to create tone-map pipeline\n");
        return false;
    }

    if (!CreatePostFX()) {
        std::fprintf(stderr, "Renderer: failed to create post-processing effects\n");
        return false;
    }

    // If the back buffer isn't an sRGB format, the tone-map shader encodes sRGB.
    m_impl->outputSRGB = !(sc.ColorBufferFormat == TEX_FORMAT_RGBA8_UNORM_SRGB ||
                           sc.ColorBufferFormat == TEX_FORMAT_BGRA8_UNORM_SRGB);
    return true;
}

// --- Offscreen targets, pipelines & effects ---------------------------------

bool Renderer::CreateOffscreenTargets(uint32_t width, uint32_t height) {
    m_impl->hdrColor.Release();
    m_impl->normalBuffer.Release();
    m_impl->sceneDepth.Release();
    m_impl->motionVectors.Release();

    TextureDesc cd;
    cd.Name      = "HDR scene color";
    cd.Type      = RESOURCE_DIM_TEX_2D;
    cd.Width     = width;
    cd.Height    = height;
    cd.MipLevels = 1;
    cd.Format    = kHDRFormat;
    cd.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;
    m_impl->device->CreateTexture(cd, nullptr, &m_impl->hdrColor);

    // World-space normal G-buffer (second scene render target), read by SSAO.
    TextureDesc nd;
    nd.Name      = "scene normals";
    nd.Type      = RESOURCE_DIM_TEX_2D;
    nd.Width     = width;
    nd.Height    = height;
    nd.MipLevels = 1;
    nd.Format    = kNormalFormat;
    nd.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;
    m_impl->device->CreateTexture(nd, nullptr, &m_impl->normalBuffer);

    // Depth doubles as a shader resource: PostFXContext reads it as an SRV (the
    // Vulkan backend exposes D32 depth as R32_FLOAT). BIND_SHADER_RESOURCE is the
    // only difference from a plain depth target.
    TextureDesc dd;
    dd.Name      = "scene depth";
    dd.Type      = RESOURCE_DIM_TEX_2D;
    dd.Width     = width;
    dd.Height    = height;
    dd.MipLevels = 1;
    dd.Format    = kSceneDepthFormat;
    dd.BindFlags = BIND_DEPTH_STENCIL | BIND_SHADER_RESOURCE;
    m_impl->device->CreateTexture(dd, nullptr, &m_impl->sceneDepth);

    // Screen-space motion-vector target (third scene render target): NDC velocity per
    // pixel, read by SSAO temporal accumulation (and DoF). Cleared + written each
    // frame by the toon passes; PostFXContext then reads it.
    TextureDesc md;
    md.Name      = "motion vectors";
    md.Type      = RESOURCE_DIM_TEX_2D;
    md.Width     = width;
    md.Height    = height;
    md.MipLevels = 1;
    md.Format    = kMotionFormat;
    md.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;
    m_impl->device->CreateTexture(md, nullptr, &m_impl->motionVectors);

    return m_impl->hdrColor && m_impl->normalBuffer && m_impl->sceneDepth && m_impl->motionVectors;
}

bool Renderer::CreatePostPipeline() {
    IRenderDevice*       device = m_impl->device;
    const SwapChainDesc& sc     = m_impl->swapChain->GetDesc();

    {
        BufferDesc cbDesc;
        cbDesc.Name           = "post constants";
        cbDesc.Size           = sizeof(PostConstants);
        cbDesc.Usage          = USAGE_DYNAMIC;
        cbDesc.BindFlags      = BIND_UNIFORM_BUFFER;
        cbDesc.CPUAccessFlags = CPU_ACCESS_WRITE;
        device->CreateBuffer(cbDesc, nullptr, &m_impl->postConstants);
        if (!m_impl->postConstants) return false;
    }

    IShaderSourceInputStreamFactory* sf = m_impl->shaderFactory;
    auto vs = CreateToonShader(device, sf, SHADER_TYPE_VERTEX, "tonemap VS", "tonemap.hlsl", "VSMain");
    auto ps = CreateToonShader(device, sf, SHADER_TYPE_PIXEL,  "tonemap PS", "tonemap.hlsl", "PSMain");
    if (!vs || !ps) return false;

    GraphicsPipelineStateCreateInfo ci;
    ci.PSODesc.Name         = "tonemap PSO";
    ci.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;

    GraphicsPipelineDesc& gp = ci.GraphicsPipeline;
    gp.NumRenderTargets              = 1;
    gp.RTVFormats[0]                 = sc.ColorBufferFormat;   // resolve to the back buffer
    gp.PrimitiveTopology             = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    gp.RasterizerDesc.CullMode       = CULL_MODE_NONE;
    gp.DepthStencilDesc.DepthEnable  = False;

    ci.pVS = vs;
    ci.pPS = ps;

    // g_HDRColor is DYNAMIC: EndScene re-points it every frame at whichever HDR
    // source we resolve (the raw scene, or Bloom's output when enabled), and that
    // texture also changes on resize. A dynamic variable is the type meant for a
    // per-frame-changing binding — Diligent manages a fresh descriptor each commit,
    // so switching it is safe. (A MUTABLE variable bakes one binding and rejects
    // being overwritten while a prior frame may still be reading it.) PostConstants
    // never changes binding, so it stays static.
    // g_HDRColor and g_AO are DYNAMIC: EndScene re-points them every frame at
    // whichever HDR source we resolve (raw scene, or Bloom's output) and at the SSAO
    // result (or a white "no occlusion" default). A dynamic variable is the type
    // meant for a per-frame-changing binding — Diligent manages a fresh descriptor
    // each commit. (A MUTABLE variable bakes one binding and rejects being
    // overwritten while a prior frame may still be reading it.) PostConstants never
    // changes binding, so it stays static.
    ShaderResourceVariableDesc vars[] = {
        { SHADER_TYPE_PIXEL, "g_HDRColor",    SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC },
        { SHADER_TYPE_PIXEL, "g_AO",          SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC },
        { SHADER_TYPE_PIXEL, "g_SSR",         SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC },
        { SHADER_TYPE_PIXEL, "PostConstants", SHADER_RESOURCE_VARIABLE_TYPE_STATIC  },
    };
    ci.PSODesc.ResourceLayout.Variables    = vars;
    ci.PSODesc.ResourceLayout.NumVariables = sizeof(vars) / sizeof(vars[0]);

    SamplerDesc linClamp;
    linClamp.MinFilter = FILTER_TYPE_LINEAR; linClamp.MagFilter = FILTER_TYPE_LINEAR; linClamp.MipFilter = FILTER_TYPE_LINEAR;
    linClamp.AddressU  = TEXTURE_ADDRESS_CLAMP; linClamp.AddressV = TEXTURE_ADDRESS_CLAMP; linClamp.AddressW = TEXTURE_ADDRESS_CLAMP;
    ImmutableSamplerDesc immSamplers[] = {
        { SHADER_TYPE_PIXEL, "g_HDRColor", linClamp },
        { SHADER_TYPE_PIXEL, "g_AO",       linClamp },
        { SHADER_TYPE_PIXEL, "g_SSR",      linClamp },
    };
    ci.PSODesc.ResourceLayout.ImmutableSamplers    = immSamplers;
    ci.PSODesc.ResourceLayout.NumImmutableSamplers = sizeof(immSamplers) / sizeof(immSamplers[0]);

    device->CreateGraphicsPipelineState(ci, &m_impl->tonemapPSO);
    if (!m_impl->tonemapPSO) return false;
    if (auto* v = m_impl->tonemapPSO->GetStaticVariableByName(SHADER_TYPE_PIXEL, "PostConstants"))
        v->Set(m_impl->postConstants);
    m_impl->tonemapPSO->CreateShaderResourceBinding(&m_impl->tonemapSRB, true);
    return m_impl->tonemapSRB != nullptr;
}

bool Renderer::CreatePostFX() {
    // Sync PSO creation: the first EndScene blocks briefly to compile the effects'
    // shaders, so they're live from the first frame they run (no async black-frames
    // to special-case). RunPostFX still falls back to the raw scene if not ready.
    PostFXContext::CreateInfo pfxCI;
    m_impl->postFX = std::make_unique<PostFXContext>(m_impl->device, pfxCI);

    Bloom::CreateInfo bloomCI;
    m_impl->bloom = std::make_unique<Bloom>(m_impl->device, bloomCI);

    ScreenSpaceAmbientOcclusion::CreateInfo ssaoCI;
    m_impl->ssao = std::make_unique<ScreenSpaceAmbientOcclusion>(m_impl->device, ssaoCI);

    DepthOfField::CreateInfo dofCI;
    m_impl->dof = std::make_unique<DepthOfField>(m_impl->device, dofCI);

    TemporalAntiAliasing::CreateInfo taaCI;
    m_impl->taa = std::make_unique<TemporalAntiAliasing>(m_impl->device, taaCI);

    ScreenSpaceReflection::CreateInfo ssrCI;
    m_impl->ssr = std::make_unique<ScreenSpaceReflection>(m_impl->device, ssrCI);

    // 1x1 constant textures bound to the resolve when the matching effect is off or
    // not ready, so the composites are no-ops without a shader branch: white = "fully
    // visible" for g_AO, black = "no reflection" for g_SSR.
    auto make1x1 = [&](const char* name, const Uint8 rgba[4], RefCntAutoPtr<ITexture>& out) {
        TextureSubResData sub{ rgba, 4 };
        TextureData       texData{ &sub, 1 };
        TextureDesc td;
        td.Name      = name;
        td.Type      = RESOURCE_DIM_TEX_2D;
        td.Width     = 1;
        td.Height    = 1;
        td.MipLevels = 1;
        td.Format    = TEX_FORMAT_RGBA8_UNORM;
        td.BindFlags = BIND_SHADER_RESOURCE;
        m_impl->device->CreateTexture(td, &texData, &out);
    };
    const Uint8 white[4] = { 255, 255, 255, 255 };
    const Uint8 black[4] = { 0, 0, 0, 0 };
    make1x1("AO white default", white, m_impl->aoWhite);
    make1x1("SSR black default", black, m_impl->ssrBlack);

    // 1x1 white 2D-ARRAY albedo default: the glTF loader stores textures as 2D arrays, so
    // the model fill's g_Albedo is a Texture2DArray — the untextured fallback must match
    // that dimension (the plain-2D aoWhite would trip a view-dimension assertion).
    {
        const Uint8       white1[4] = { 255, 255, 255, 255 };
        TextureSubResData sub{ white1, 4 };
        TextureData       texData{ &sub, 1 };
        TextureDesc td;
        td.Name      = "model albedo white default";
        td.Type      = RESOURCE_DIM_TEX_2D_ARRAY;
        td.Width     = 1;
        td.Height    = 1;
        td.ArraySize = 1;
        td.MipLevels = 1;
        td.Format    = TEX_FORMAT_RGBA8_UNORM;
        td.BindFlags = BIND_SHADER_RESOURCE;
        m_impl->device->CreateTexture(td, &texData, &m_impl->modelWhite);
    }

    return m_impl->postFX && m_impl->bloom && m_impl->ssao && m_impl->dof && m_impl->taa &&
           m_impl->ssr && m_impl->aoWhite && m_impl->ssrBlack && m_impl->modelWhite;
}

bool Renderer::CreateToonPipeline() {
    IRenderDevice* device = m_impl->device;

    // Shared, per-draw-updated constant buffer.
    {
        BufferDesc cbDesc;
        cbDesc.Name           = "toon constants";
        cbDesc.Size           = sizeof(ShaderConstants);
        cbDesc.Usage          = USAGE_DYNAMIC;
        cbDesc.BindFlags      = BIND_UNIFORM_BUFFER;
        cbDesc.CPUAccessFlags = CPU_ACCESS_WRITE;
        device->CreateBuffer(cbDesc, nullptr, &m_impl->constants);
        if (!m_impl->constants) return false;
    }

    // Vertex input layout: mirrors toon::Vertex (two tightly-packed float3s;
    // offsets/stride auto-computed from element order).
    LayoutElement layoutElems[] = {
        LayoutElement{0, 0, 3, VT_FLOAT32, False},   // ATTRIB0 position
        LayoutElement{1, 0, 3, VT_FLOAT32, False},   // ATTRIB1 normal (shading)
        LayoutElement{2, 0, 3, VT_FLOAT32, False},   // ATTRIB2 smooth normal (outline hull)
    };

    // Build a graphics PSO for one toon pass and wire the shared CB into it.
    auto buildPass = [&](const char* name, IShader* vs, IShader* ps, CULL_MODE cull,
                         RefCntAutoPtr<IPipelineState>&         pso,
                         RefCntAutoPtr<IShaderResourceBinding>& srb) -> bool {
        GraphicsPipelineStateCreateInfo ci;
        ci.PSODesc.Name         = name;
        ci.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;

        GraphicsPipelineDesc& gp = ci.GraphicsPipeline;
        gp.NumRenderTargets                     = 3;                  // MRT: color + normals + motion
        gp.RTVFormats[0]                        = kHDRFormat;         // SV_Target0: HDR scene color
        gp.RTVFormats[1]                        = kNormalFormat;      // SV_Target1: world-space normals
        gp.RTVFormats[2]                        = kMotionFormat;      // SV_Target2: NDC motion vectors
        gp.DSVFormat                            = kSceneDepthFormat;
        gp.PrimitiveTopology                    = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        gp.RasterizerDesc.CullMode              = cull;
        gp.RasterizerDesc.FrontCounterClockwise = True;   // primitives are CCW when seen from outside
        gp.DepthStencilDesc.DepthEnable         = True;
        gp.DepthStencilDesc.DepthWriteEnable    = True;
        gp.DepthStencilDesc.DepthFunc           = COMPARISON_FUNC_LESS_EQUAL;
        gp.InputLayout.LayoutElements           = layoutElems;
        gp.InputLayout.NumElements              = sizeof(layoutElems) / sizeof(layoutElems[0]);

        ci.pVS = vs;
        ci.pPS = ps;
        // One shared static CB across both stages; set once on the PSO below.
        ci.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;

        device->CreateGraphicsPipelineState(ci, &pso);
        if (!pso) return false;
        if (auto* v = pso->GetStaticVariableByName(SHADER_TYPE_VERTEX, "Constants")) v->Set(m_impl->constants);
        if (auto* p = pso->GetStaticVariableByName(SHADER_TYPE_PIXEL,  "Constants")) p->Set(m_impl->constants);
        pso->CreateShaderResourceBinding(&srb, true);
        return srb != nullptr;
    };

    IShaderSourceInputStreamFactory* sf = m_impl->shaderFactory;

    auto fillVS = CreateToonShader(device, sf, SHADER_TYPE_VERTEX, "toon fill VS", "toon_fill.hlsl", "VSMain");
    auto fillPS = CreateToonShader(device, sf, SHADER_TYPE_PIXEL,  "toon fill PS", "toon_fill.hlsl", "PSMain");
    auto outVS  = CreateToonShader(device, sf, SHADER_TYPE_VERTEX, "toon outline VS", "toon_outline.hlsl", "VSMain");
    auto outPS  = CreateToonShader(device, sf, SHADER_TYPE_PIXEL,  "toon outline PS", "toon_outline.hlsl", "PSMain");
    if (!fillVS || !fillPS || !outVS || !outPS) return false;

    // Fill: cull back faces. Outline: cull front faces (keep the enlarged shell).
    if (!buildPass("toon fill PSO",    fillVS, fillPS, CULL_MODE_BACK,  m_impl->fillPSO,    m_impl->fillSRB))    return false;
    if (!buildPass("toon outline PSO", outVS,  outPS,  CULL_MODE_FRONT, m_impl->outlinePSO, m_impl->outlineSRB)) return false;
    return true;
}

bool Renderer::CreateModelPipeline() {
    IRenderDevice*                   device = m_impl->device;
    IShaderSourceInputStreamFactory* sf     = m_impl->shaderFactory;

    auto vs = CreateToonShader(device, sf, SHADER_TYPE_VERTEX, "model fill VS", "model_fill.hlsl", "VSMain");
    auto ps = CreateToonShader(device, sf, SHADER_TYPE_PIXEL,  "model fill PS", "model_fill.hlsl", "PSMain");
    if (!vs || !ps) return false;

    // Input layout matching the interleaved buffer the loader fills from ModelVertexAttribs
    // (POSITION/NORMAL/TEXCOORD_0 in buffer 0). Auto offset/stride reproduce the loader's
    // packing exactly (pos@0, normal@12, uv@24, stride 32), so we hardcode it — same idiom
    // as CreateToonPipeline.
    LayoutElement modelLayout[] = {
        LayoutElement{0, 0, 3, VT_FLOAT32, False},   // ATTRIB0 position
        LayoutElement{1, 0, 3, VT_FLOAT32, False},   // ATTRIB1 normal
        LayoutElement{2, 0, 2, VT_FLOAT32, False},   // ATTRIB2 uv
    };

    GraphicsPipelineStateCreateInfo ci;
    ci.PSODesc.Name         = "model fill PSO";
    ci.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;

    GraphicsPipelineDesc& gp = ci.GraphicsPipeline;
    gp.NumRenderTargets                     = 3;             // MRT: color + normals + motion (same as toon)
    gp.RTVFormats[0]                        = kHDRFormat;
    gp.RTVFormats[1]                        = kNormalFormat;
    gp.RTVFormats[2]                        = kMotionFormat;
    gp.DSVFormat                            = kSceneDepthFormat;
    gp.PrimitiveTopology                    = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    gp.RasterizerDesc.CullMode              = CULL_MODE_BACK;
    // glTF winds front faces CCW in its RIGHT-handed space; our projection is LEFT-handed,
    // which flips screen-space winding, so the model's outward faces are CW here. Hence
    // FrontCounterClockwise = False — the OPPOSITE of our own primitives (authored CCW-front
    // for the LH setup). With True, the outward faces cull and you see through the helmet to
    // its inner surface.
    gp.RasterizerDesc.FrontCounterClockwise = False;
    gp.DepthStencilDesc.DepthEnable         = True;
    gp.DepthStencilDesc.DepthWriteEnable    = True;
    gp.DepthStencilDesc.DepthFunc           = COMPARISON_FUNC_LESS_EQUAL;
    gp.InputLayout.LayoutElements           = modelLayout;
    gp.InputLayout.NumElements              = sizeof(modelLayout) / sizeof(modelLayout[0]);

    ci.pVS = vs;
    ci.pPS = ps;

    // Shared static Constants CB (like the toon PSOs); g_Albedo is DYNAMIC — DrawModel
    // re-Sets it per primitive — with a linear-wrap immutable sampler (combined-sampler
    // "g_Albedo", same pattern as tonemap's g_HDRColor).
    ShaderResourceVariableDesc vars[] = {
        { SHADER_TYPE_VERTEX, "Constants", SHADER_RESOURCE_VARIABLE_TYPE_STATIC  },
        { SHADER_TYPE_PIXEL,  "Constants", SHADER_RESOURCE_VARIABLE_TYPE_STATIC  },
        { SHADER_TYPE_PIXEL,  "g_Albedo",  SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC },
    };
    ci.PSODesc.ResourceLayout.Variables    = vars;
    ci.PSODesc.ResourceLayout.NumVariables = sizeof(vars) / sizeof(vars[0]);

    SamplerDesc linWrap;
    linWrap.MinFilter = FILTER_TYPE_LINEAR; linWrap.MagFilter = FILTER_TYPE_LINEAR; linWrap.MipFilter = FILTER_TYPE_LINEAR;
    linWrap.AddressU  = TEXTURE_ADDRESS_WRAP; linWrap.AddressV = TEXTURE_ADDRESS_WRAP; linWrap.AddressW = TEXTURE_ADDRESS_WRAP;
    ImmutableSamplerDesc immSamplers[] = {
        { SHADER_TYPE_PIXEL, "g_Albedo", linWrap },
    };
    ci.PSODesc.ResourceLayout.ImmutableSamplers    = immSamplers;
    ci.PSODesc.ResourceLayout.NumImmutableSamplers = sizeof(immSamplers) / sizeof(immSamplers[0]);

    device->CreateGraphicsPipelineState(ci, &m_impl->modelPSO);
    if (!m_impl->modelPSO) return false;
    if (auto* v = m_impl->modelPSO->GetStaticVariableByName(SHADER_TYPE_VERTEX, "Constants")) v->Set(m_impl->constants);
    if (auto* p = m_impl->modelPSO->GetStaticVariableByName(SHADER_TYPE_PIXEL,  "Constants")) p->Set(m_impl->constants);
    m_impl->modelPSO->CreateShaderResourceBinding(&m_impl->modelSRB, true);
    if (!m_impl->modelSRB) return false;

    // --- Outline pass PSO (inverted hull) --- same vertex layout; cull FRONT to keep the
    // enlarged back-facing shell; only the shared Constants CB (no albedo texture).
    auto ovs = CreateToonShader(device, sf, SHADER_TYPE_VERTEX, "model outline VS", "model_outline.hlsl", "VSMain");
    auto ops = CreateToonShader(device, sf, SHADER_TYPE_PIXEL,  "model outline PS", "model_outline.hlsl", "PSMain");
    if (!ovs || !ops) return false;

    GraphicsPipelineStateCreateInfo oci;
    oci.PSODesc.Name         = "model outline PSO";
    oci.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;

    GraphicsPipelineDesc& ogp = oci.GraphicsPipeline;
    ogp.NumRenderTargets                     = 3;
    ogp.RTVFormats[0]                        = kHDRFormat;
    ogp.RTVFormats[1]                        = kNormalFormat;
    ogp.RTVFormats[2]                        = kMotionFormat;
    ogp.DSVFormat                            = kSceneDepthFormat;
    ogp.PrimitiveTopology                    = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    ogp.RasterizerDesc.CullMode              = CULL_MODE_FRONT;   // keep the enlarged back shell
    ogp.RasterizerDesc.FrontCounterClockwise = False;            // model winding (matches the fill)
    ogp.DepthStencilDesc.DepthEnable         = True;
    ogp.DepthStencilDesc.DepthWriteEnable    = True;
    ogp.DepthStencilDesc.DepthFunc           = COMPARISON_FUNC_LESS_EQUAL;
    ogp.InputLayout.LayoutElements           = modelLayout;
    ogp.InputLayout.NumElements              = sizeof(modelLayout) / sizeof(modelLayout[0]);

    oci.pVS = ovs;
    oci.pPS = ops;

    ShaderResourceVariableDesc ovars[] = {
        { SHADER_TYPE_VERTEX, "Constants", SHADER_RESOURCE_VARIABLE_TYPE_STATIC },
        { SHADER_TYPE_PIXEL,  "Constants", SHADER_RESOURCE_VARIABLE_TYPE_STATIC },
    };
    oci.PSODesc.ResourceLayout.Variables    = ovars;
    oci.PSODesc.ResourceLayout.NumVariables = sizeof(ovars) / sizeof(ovars[0]);

    device->CreateGraphicsPipelineState(oci, &m_impl->modelOutlinePSO);
    if (!m_impl->modelOutlinePSO) return false;
    if (auto* v = m_impl->modelOutlinePSO->GetStaticVariableByName(SHADER_TYPE_VERTEX, "Constants")) v->Set(m_impl->constants);
    if (auto* p = m_impl->modelOutlinePSO->GetStaticVariableByName(SHADER_TYPE_PIXEL,  "Constants")) p->Set(m_impl->constants);
    m_impl->modelOutlinePSO->CreateShaderResourceBinding(&m_impl->modelOutlineSRB, true);
    return m_impl->modelOutlineSRB != nullptr;
}

// --- Teardown ---------------------------------------------------------------

void Renderer::Shutdown() {
    if (!m_impl) return;
    // Wait for the GPU to finish the last submitted frame before releasing anything.
    // Its commands may still be reading the HDR / bloom / depth targets; releasing
    // those while they're in flight trips Diligent's in-use checks and pops the
    // debug abort dialog on window close. WaitForIdle also flushes.
    if (m_impl->context) m_impl->context->WaitForIdle();
    ShutdownUI(); // must release ImGui's GPU resources before the device

    // Release scene/pipeline GPU objects before the device.
    m_impl->meshes.clear();
    m_impl->models.clear();           // GLTF::Model objects own GPU buffers + textures
    m_impl->bloom.reset();            // DiligentFX effect objects own GPU resources
    m_impl->ssao.reset();
    m_impl->dof.reset();
    m_impl->taa.reset();
    m_impl->ssr.reset();
    m_impl->postFX.reset();
    m_impl->motionVectors.Release();
    m_impl->aoWhite.Release();
    m_impl->ssrBlack.Release();
    m_impl->modelWhite.Release();
    m_impl->tonemapSRB.Release();
    m_impl->tonemapPSO.Release();
    m_impl->postConstants.Release();
    m_impl->hdrColor.Release();
    m_impl->normalBuffer.Release();
    m_impl->sceneDepth.Release();
    m_impl->fillSRB.Release();
    m_impl->outlineSRB.Release();
    m_impl->modelSRB.Release();
    m_impl->modelOutlineSRB.Release();
    m_impl->fillPSO.Release();
    m_impl->outlinePSO.Release();
    m_impl->modelPSO.Release();
    m_impl->modelOutlinePSO.Release();
    m_impl->constants.Release();
    m_impl->shaderFactory.Release();

    m_impl->swapChain.Release();
    m_impl->context.Release();
    m_impl->device.Release();
}

// --- Per-frame lifecycle ----------------------------------------------------

void Renderer::BeginFrame(const Color& c) {
    // The scene renders into three offscreen targets (consumed in EndScene): HDR
    // color, a world-space normal G-buffer (SSAO), and NDC motion vectors (SSAO
    // temporal / DoF), sharing the scene depth buffer.
    ITextureView* rtvs[] = { m_impl->hdrColor->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET),
                             m_impl->normalBuffer->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET),
                             m_impl->motionVectors->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET) };
    ITextureView* dsv = m_impl->sceneDepth->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL);
    m_impl->context->SetRenderTargets(3, rtvs, dsv, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    const float clear[] = { c.r, c.g, c.b, c.a };
    const float zero[]  = { 0.0f, 0.0f, 0.0f, 0.0f };   // background: zero normal + zero motion
    m_impl->context->ClearRenderTarget(rtvs[0], clear, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    m_impl->context->ClearRenderTarget(rtvs[1], zero,  RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    m_impl->context->ClearRenderTarget(rtvs[2], zero,  RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    m_impl->context->ClearDepthStencil(dsv, CLEAR_DEPTH_FLAG, 1.0f, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
}

void Renderer::SetPostParams(const PostParams& params) {
    m_impl->post = params;
}

void Renderer::EndScene() {
    // Run the enabled post effects first (they bind their own render targets, so
    // this must precede binding the back buffer below). RunPostFX yields:
    //  - colorSRV: the scene after DoF + Bloom (a drop-in HDR resolve input, so
    //    tonemap.hlsl is unchanged), or null -> resolve the raw scene.
    //  - aoSRV: SSAO visibility, or null -> the white "no occlusion" default.
    //  - ssrSRV: SSR reflection radiance, or null -> the black "no reflection" default.
    ITextureView* colorSRV = nullptr;
    ITextureView* aoSRV    = nullptr;
    ITextureView* ssrSRV   = nullptr;
    m_impl->RunPostFX(m_impl->post, colorSRV, aoSRV, ssrSRV);

    // Resolve inputs: processed color or raw scene; real AO/SSR or their 1x1 defaults.
    ITextureView* postInput = colorSRV ? colorSRV : m_impl->hdrColor->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
    ITextureView* aoInput   = aoSRV    ? aoSRV    : m_impl->aoWhite->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
    ITextureView* ssrInput  = ssrSRV   ? ssrSRV   : m_impl->ssrBlack->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);

    // Resolve the HDR source to the back buffer: SSAO darkening + exposure + tone
    // map. Leaves the back-buffer RTV bound so the UI overlay draws on top.
    ITextureView* rtv = m_impl->swapChain->GetCurrentBackBufferRTV();
    m_impl->context->SetRenderTargets(1, &rtv, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    {
        MapHelper<PostConstants> cb(m_impl->context, m_impl->postConstants, MAP_WRITE, MAP_FLAG_DISCARD);
        cb->exposure     = m_impl->post.exposure;
        cb->toneMap      = m_impl->post.toneMap ? 1.0f : 0.0f;
        cb->outputSRGB   = m_impl->outputSRGB ? 1.0f : 0.0f;
        // Only apply strength when the matching effect produced a real texture (else
        // the default is bound and the composite is a no-op anyway).
        cb->ssaoStrength = aoSRV  ? m_impl->post.ssaoStrength : 0.0f;
        cb->ssrStrength  = ssrSRV ? m_impl->post.ssrStrength  : 0.0f;
        cb->pad0 = cb->pad1 = cb->pad2 = 0.0f;
    }

    // Point the resolve at this frame's HDR source + AO + SSR. All dynamic, so they
    // may safely differ from last frame (scene <-> bloom, effects on/off, or a resize).
    if (auto* v = m_impl->tonemapSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_HDRColor"))
        v->Set(postInput);
    if (auto* v = m_impl->tonemapSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_AO"))
        v->Set(aoInput);
    if (auto* v = m_impl->tonemapSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_SSR"))
        v->Set(ssrInput);

    m_impl->context->SetPipelineState(m_impl->tonemapPSO);
    m_impl->context->CommitShaderResources(m_impl->tonemapSRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    DrawAttribs draw;
    draw.NumVertices = 3;               // full-screen triangle
    draw.Flags       = DRAW_FLAG_VERIFY_ALL;
    m_impl->context->Draw(draw);
}

void Renderer::EndFrame() {
    m_impl->swapChain->Present(); // vsync on by default
}

void Renderer::Resize(uint32_t width, uint32_t height) {
    if (!m_impl->swapChain || width == 0 || height == 0) return;
    m_impl->swapChain->Resize(width, height);
    const SwapChainDesc& sc = m_impl->swapChain->GetDesc();
    CreateOffscreenTargets(sc.Width, sc.Height);   // match the new back-buffer size
    // The resolve's HDR input (g_HDRColor) is dynamic — EndScene re-points it at the
    // recreated target next frame, so there's nothing to rebind here.
}

// --- Scene: meshes, camera, lighting, draw ----------------------------------

MeshHandle Renderer::CreateMesh(const Vertex* vertices, uint32_t vertexCount,
                                const uint32_t* indices, uint32_t indexCount) {
    if (!vertices || vertexCount == 0 || !indices || indexCount == 0)
        return MeshHandle::Invalid;

    Impl::GpuMesh mesh;
    mesh.indexCount = indexCount;

    BufferDesc vbDesc;
    vbDesc.Name      = "toon mesh VB";
    vbDesc.Usage     = USAGE_IMMUTABLE;
    vbDesc.BindFlags = BIND_VERTEX_BUFFER;
    vbDesc.Size      = static_cast<Uint64>(vertexCount) * sizeof(Vertex);
    BufferData vbData{vertices, vbDesc.Size};
    m_impl->device->CreateBuffer(vbDesc, &vbData, &mesh.vertexBuffer);

    BufferDesc ibDesc;
    ibDesc.Name      = "toon mesh IB";
    ibDesc.Usage     = USAGE_IMMUTABLE;
    ibDesc.BindFlags = BIND_INDEX_BUFFER;
    ibDesc.Size      = static_cast<Uint64>(indexCount) * sizeof(uint32_t);
    BufferData ibData{indices, ibDesc.Size};
    m_impl->device->CreateBuffer(ibDesc, &ibData, &mesh.indexBuffer);

    if (!mesh.vertexBuffer || !mesh.indexBuffer)
        return MeshHandle::Invalid;

    m_impl->meshes.push_back(std::move(mesh));
    return static_cast<MeshHandle>(m_impl->meshes.size()); // 1-based; 0 = Invalid
}

void Renderer::SetCamera(const Camera& cam) {
    const SwapChainDesc& sc = m_impl->swapChain->GetDesc();
    const float aspect = sc.Height > 0 ? static_cast<float>(sc.Width) / static_cast<float>(sc.Height) : 1.0f;

    // Orbit the pivot: translate the world so the pivot sits at the origin, then the
    // turntable (rotate about the origin, push `distance` down +Z in front of the
    // left-handed camera at the origin). Pan/fly move the pivot; zoom changes distance.
    const float4x4 view = float4x4::Translation(-cam.pivot.x, -cam.pivot.y, -cam.pivot.z) *
                          float4x4::RotationY(cam.yaw) *
                          float4x4::RotationX(cam.pitch) *
                          float4x4::Translation(0.0f, 0.0f, cam.distance);
    // NegativeOneToOneZ = false -> [0,1] depth range for Vulkan/D3D.
    float4x4 proj = float4x4::Projection(cam.fovY, aspect, cam.nearZ, cam.farZ, false);

    // TAA: jitter the projection by a sub-pixel offset so accumulated frames cover
    // different sample positions. GetJitterOffset returns 0 until TAA is ready (and
    // we only jitter when it's on), so the scene isn't shifted otherwise. The jitter
    // is recorded in the camera attribs (FillCameraAttribs) so PostFX can undo it.
    m_impl->frameJitter = float2(0.0f, 0.0f);
    if (m_impl->post.taa && m_impl->taa) {
        m_impl->frameJitter = m_impl->taa->GetJitterOffset();
        proj = TemporalAntiAliasing::GetJitteredProjMatrix(proj, m_impl->frameJitter);
    }

    // Keep view/proj (and near/far) split, not just their product: the SSAO camera
    // attribs (FillCameraAttribs) need each matrix and its inverse. Snapshot the old
    // viewProj first so motion vectors capture camera motion too. SetCamera runs once
    // per frame before the draws, so this is last frame's value.
    m_impl->prevViewProj = m_impl->viewProj;
    m_impl->view         = view;
    m_impl->proj         = proj;
    m_impl->viewProj     = view * proj;
    m_impl->nearZ        = cam.nearZ;
    m_impl->farZ         = cam.farZ;
}

void Renderer::SetLight(const Vec3& directionToLight) {
    m_impl->lightDir = float3(directionToLight.x, directionToLight.y, directionToLight.z);
}

// Object -> world (Diligent is row-major / row-vector: v * M). Rotation order X,Y,Z.
static float4x4 WorldFromTransform(const Transform& t) {
    return float4x4::Scale(t.scale.x, t.scale.y, t.scale.z) *
           float4x4::RotationX(t.rotationEuler.x) *
           float4x4::RotationY(t.rotationEuler.y) *
           float4x4::RotationZ(t.rotationEuler.z) *
           float4x4::Translation(t.position.x, t.position.y, t.position.z);
}

// Plain Mat4 (seam vocabulary) <-> Diligent float4x4 — both row-major, so a straight
// element copy. The scene graph composes world matrices on the Diligent side and hands
// them across the seam as Mat4.
static Mat4 ToMat4(const float4x4& m) {
    Mat4 out;
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            out.m[r * 4 + c] = m[r][c];
    return out;
}
static float4x4 ToFloat4x4(const Mat4& in) {
    float4x4 out;
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            out[r][c] = in.m[r * 4 + c];
    return out;
}

// The toon draw, given a pre-composed object->world matrix (+ last frame's, for motion
// vectors). The scene graph passes hierarchy-composed world matrices straight in; the
// Transform overload below builds them from a single object's placement.
void Renderer::DrawMesh(MeshHandle handle, const Mat4& worldM, const Mat4& prevWorldM, const Material& mat) {
    const uint32_t idx = static_cast<uint32_t>(handle);
    if (idx == 0 || idx > m_impl->meshes.size())
        return;
    const Impl::GpuMesh& mesh = m_impl->meshes[idx - 1];

    const float4x4 world   = ToFloat4x4(worldM);
    const float4x4 wvp     = world * m_impl->viewProj;
    const float4x4 prevWvp = ToFloat4x4(prevWorldM) * m_impl->prevViewProj;

    // Normal matrix = inverse-transpose of the world matrix (correct normals under
    // non-uniform scale; its 3x3 transpose is world^-1, used by the outline VS).
    const float4x4 normalMat = world.Inverse().Transpose();

    {
        const float3& L = m_impl->lightDir;
        MapHelper<ShaderConstants> cb(m_impl->context, m_impl->constants, MAP_WRITE, MAP_FLAG_DISCARD);
        cb->worldViewProj     = wvp;
        cb->world             = world;
        cb->normalMatrix      = normalMat;
        cb->prevWorldViewProj = prevWvp;
        cb->lightDir          = float4(L.x, L.y, L.z, 0.0f);
        cb->baseColor         = float4(mat.baseColor.x, mat.baseColor.y, mat.baseColor.z, 1.0f);
        cb->outline           = float4(mat.outlineColor.x, mat.outlineColor.y, mat.outlineColor.z, mat.outlineWidth);
        cb->params            = float4(mat.bands, mat.ambient, mat.roughness, 0.0f);
    }

    IBuffer*     vbs[]     = { mesh.vertexBuffer };
    const Uint64 offsets[] = { 0 };
    m_impl->context->SetVertexBuffers(0, 1, vbs, offsets,
                                      RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                                      SET_VERTEX_BUFFERS_FLAG_RESET);
    m_impl->context->SetIndexBuffer(mesh.indexBuffer, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    DrawIndexedAttribs draw;
    draw.IndexType  = VT_UINT32;
    draw.NumIndices = mesh.indexCount;
    draw.Flags      = DRAW_FLAG_VERIFY_ALL;

    // Outline first (enlarged back-facing shell), then the fill on top — the
    // fill's nearer depth overwrites the shell everywhere except the rim.
    m_impl->context->SetPipelineState(m_impl->outlinePSO);
    m_impl->context->CommitShaderResources(m_impl->outlineSRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    m_impl->context->DrawIndexed(draw);

    m_impl->context->SetPipelineState(m_impl->fillPSO);
    m_impl->context->CommitShaderResources(m_impl->fillSRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    m_impl->context->DrawIndexed(draw);
}

// Convenience: a single object's placement. Builds world matrices from the transforms
// (no parent) and delegates to the Mat4 overload above.
void Renderer::DrawMesh(MeshHandle handle, const Transform& t, const Transform& prevT, const Material& mat) {
    DrawMesh(handle, ToMat4(WorldFromTransform(t)), ToMat4(WorldFromTransform(prevT)), mat);
}

// --- Scene: glTF models -----------------------------------------------------

ModelHandle Renderer::LoadModel(const char* path) {
    if (!path) return ModelHandle::Invalid;

    size_t attribCount = 0;
    const GLTF::VertexAttributeDesc* attribs = ModelVertexAttribs(attribCount);

    GLTF::ModelCreateInfo ci;
    ci.FileName            = path;
    ci.VertexAttributes    = attribs;
    ci.NumVertexAttributes = static_cast<Uint32>(attribCount);
    ci.IndexType           = VT_UINT32;
    // The loader defaults vertex buffers to BIND_NONE (only the index buffer defaults to
    // BIND_INDEX_BUFFER); without this the VB can't be bound/drawn. We pack everything into
    // buffer 0, so flag slot 0.
    ci.VertBufferBindFlags[0] = BIND_VERTEX_BUFFER;

    // The loader parses + uploads GPU buffers/textures in its constructor; it throws on a
    // bad/missing file, so guard the load.
    std::unique_ptr<GLTF::Model> model;
    try {
        model = std::make_unique<GLTF::Model>(m_impl->device, m_impl->context, ci);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Renderer: failed to load model '%s': %s\n", path, e.what());
        return ModelHandle::Invalid;
    }
    model->PrepareGPUResources(m_impl->device, m_impl->context);

    m_impl->models.push_back(std::move(model));
    return static_cast<ModelHandle>(m_impl->models.size());   // 1-based; 0 = Invalid
}

void Renderer::DrawModel(ModelHandle handle, const Mat4& worldM, const Mat4& prevWorldM, const Material& style) {
    const uint32_t idx = static_cast<uint32_t>(handle);
    if (idx == 0 || idx > m_impl->models.size())
        return;
    GLTF::Model& model = *m_impl->models[idx - 1];
    if (model.Scenes.empty())
        return;
    const int sceneId = model.DefaultSceneId;

    // Object placement this frame + last (for motion vectors), composed with each node's
    // transform inside the model.
    const float4x4 objWorld     = ToFloat4x4(worldM);
    const float4x4 objPrevWorld = ToFloat4x4(prevWorldM);

    GLTF::ModelTransforms xforms;
    model.ComputeTransforms(sceneId, xforms);

    // Bind the model's shared vertex + index buffers once; every primitive sub-ranges them.
    IBuffer*     vbs[8] = {};
    const Uint32 numVBs = static_cast<Uint32>(model.GetVertexBufferCount());
    for (Uint32 i = 0; i < numVBs; ++i)
        vbs[i] = model.GetVertexBuffer(i, m_impl->device, m_impl->context);
    m_impl->context->SetVertexBuffers(0, numVBs, vbs, nullptr,
                                      RESOURCE_STATE_TRANSITION_MODE_TRANSITION, SET_VERTEX_BUFFERS_FLAG_RESET);
    IBuffer* ib = model.GetIndexBuffer(m_impl->device, m_impl->context);
    if (ib)
        m_impl->context->SetIndexBuffer(ib, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    const Uint32 baseIndex  = model.GetFirstIndexLocation();
    const Uint32 baseVertex = model.GetBaseVertex();

    // One primitive's indexed (or non-indexed) draw with the loader's global offsets;
    // issued once per pass (outline, then fill) after that pass's PSO + SRB are bound.
    auto issueDraw = [&](const GLTF::Primitive& p) {
        if (p.IndexCount > 0) {
            DrawIndexedAttribs draw;
            draw.IndexType          = VT_UINT32;
            draw.NumIndices         = p.IndexCount;
            draw.FirstIndexLocation = baseIndex + p.FirstIndex;
            draw.BaseVertex         = baseVertex + p.FirstVertex;
            draw.Flags              = DRAW_FLAG_VERIFY_ALL;
            m_impl->context->DrawIndexed(draw);
        } else {
            DrawAttribs draw;
            draw.NumVertices         = p.VertexCount;
            draw.StartVertexLocation = baseVertex + p.FirstVertex;
            draw.Flags               = DRAW_FLAG_VERIFY_ALL;
            m_impl->context->Draw(draw);
        }
    };

    ITextureView*      whiteSRV = m_impl->modelWhite->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
    const float3&      L        = m_impl->lightDir;
    const GLTF::Scene& scene    = model.Scenes[sceneId];

    for (const GLTF::Node* node : scene.LinearNodes) {
        if (node->pMesh == nullptr) continue;
        const float4x4 nodeGlobal = xforms.NodeGlobalMatrices[node->Index];
        const float4x4 world      = nodeGlobal * objWorld;
        const float4x4 prevWorld  = nodeGlobal * objPrevWorld;   // static model: node xform is constant
        const float4x4 normalMat  = world.Inverse().Transpose();

        for (const GLTF::Primitive& prim : node->pMesh->Primitives) {
            if (prim.VertexCount == 0 && prim.IndexCount == 0) continue;
            const GLTF::Material& mat = model.Materials[prim.MaterialId];
            const float4          bc  = mat.Attribs.BaseColorFactor;

            {
                MapHelper<ShaderConstants> cb(m_impl->context, m_impl->constants, MAP_WRITE, MAP_FLAG_DISCARD);
                cb->worldViewProj     = world * m_impl->viewProj;
                cb->world             = world;
                cb->normalMatrix      = normalMat;
                cb->prevWorldViewProj = prevWorld * m_impl->prevViewProj;
                cb->lightDir          = float4(L.x, L.y, L.z, 0.0f);
                cb->baseColor         = float4(bc.x * style.baseColor.x, bc.y * style.baseColor.y,
                                               bc.z * style.baseColor.z, bc.w);   // glTF factor * app tint
                cb->outline           = float4(style.outlineColor.x, style.outlineColor.y,
                                               style.outlineColor.z, style.outlineWidth);   // outline pass
                cb->params            = float4(style.bands, style.ambient, style.roughness, 0.0f);
            }

            // Fill-pass albedo: the material's base-color texture, or the 1x1 white
            // 2D-array fallback when it has none.
            ITextureView* albedoSRV = whiteSRV;
            const int tid = mat.GetTextureId(GLTF::DefaultBaseColorTextureAttribId);
            if (tid >= 0)
                if (ITexture* tex = model.GetTexture(static_cast<Uint32>(tid), m_impl->device, m_impl->context))
                    albedoSRV = tex->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
            if (auto* v = m_impl->modelSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_Albedo"))
                v->Set(albedoSRV);

            // Outline first (enlarged back-facing shell), then the textured fill on top —
            // the fill's nearer depth overwrites the shell everywhere but the silhouette rim.
            m_impl->context->SetPipelineState(m_impl->modelOutlinePSO);
            m_impl->context->CommitShaderResources(m_impl->modelOutlineSRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            issueDraw(prim);

            m_impl->context->SetPipelineState(m_impl->modelPSO);
            m_impl->context->CommitShaderResources(m_impl->modelSRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            issueDraw(prim);
        }
    }
}

// Convenience: a single model instance's placement (no scene parent).
void Renderer::DrawModel(ModelHandle handle, const Transform& t, const Transform& prevT, const Material& style) {
    DrawModel(handle, ToMat4(WorldFromTransform(t)), ToMat4(WorldFromTransform(prevT)), style);
}

// --- Debug UI (Dear ImGui) --------------------------------------------------

bool Renderer::InitUI(GLFWwindow* window) {
    // ImGuiImplDiligent's constructor calls ImGui::CreateContext() — it must
    // run before ImGui_ImplGlfw_InitForVulkan(), which itself calls
    // ImGui::GetIO() and asserts if no context exists yet.
    //
    // Build the ImGui PSO for how the UI is actually drawn (EndScene): to the back
    // buffer, with NO depth attachment. Passing the swap chain's depth format here
    // instead makes Diligent warn every frame that the bound DSV (none) doesn't
    // match the PSO, so pass TEX_FORMAT_UNKNOWN for depth.
    const SwapChainDesc& sc = m_impl->swapChain->GetDesc();
    ImGuiDiligentCreateInfo imguiCI{ m_impl->device, sc.ColorBufferFormat, TEX_FORMAT_UNKNOWN };
    m_impl->imgui = std::make_unique<ImGuiImplDiligent>(imguiCI);

    if (!ImGui_ImplGlfw_InitForVulkan(window, /*install_callbacks=*/true)) {
        std::fprintf(stderr, "Renderer: ImGui GLFW backend init failed\n");
        m_impl->imgui.reset();
        return false;
    }
    return true;
}

void Renderer::ShutdownUI() {
    if (!m_impl->imgui) return;
    // Tear down in the reverse of InitUI. The GLFW platform backend must be shut
    // down *before* the ImGui context is destroyed: ~ImGuiImplDiligent() calls
    // ImGui::DestroyContext(), which asserts ("Forgot to shutdown Platform
    // backend?") if the GLFW backend is still registered — that assert aborts the
    // process on window close.
    ImGui_ImplGlfw_Shutdown();
    m_impl->imgui.reset(); // destroys the Diligent ImGui backend + the ImGui context
}

void Renderer::BeginUI() {
    ImGui_ImplGlfw_NewFrame();
    const SwapChainDesc& scDesc = m_impl->swapChain->GetDesc();
    m_impl->imgui->NewFrame(scDesc.Width, scDesc.Height, scDesc.PreTransform);
}

void Renderer::EndUI() {
    m_impl->imgui->Render(m_impl->context);
}

} // namespace toon