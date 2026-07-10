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
// (two row-major float4x4 rows + four float4 rows = 192 bytes, 16-aligned).
struct ShaderConstants {
    float4x4 worldViewProj;
    float4x4 world;
    float4x4 prevWorldViewProj;   // previous frame, for motion vectors
    float4   lightDir;
    float4   baseColor;
    float4   outline;      // rgb color, w = extrude width
    float4   params;       // x = bands, y = ambient
};

// GPU mirror of tonemap.hlsl's PostConstants.
struct PostConstants {
    float exposure;
    float toneMap;       // 1 = ACES
    float outputSRGB;    // 1 = encode sRGB in-shader
    float ssaoStrength;  // 0 = AO ignored, 1 = full occlusion
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
    RefCntAutoPtr<ITexture>        motionVectors;   // RG16F, zero (no real motion)
    RefCntAutoPtr<ITexture>        aoWhite;         // 1x1 white = "fully visible" default
    HLSL::CameraAttribs            postCamera{};
    Uint32                         frameIndex = 0;

    // Run the shared PostFXContext plus whichever effects are enabled. Outputs the
    // composited scene+bloom SRV (or null -> resolve the raw scene) and the AO SRV
    // (or null -> bind the white default). No-op if neither effect is enabled.
    void RunPostFX(const PostParams& p, ITextureView*& bloomOut, ITextureView*& aoOut);

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
}

void Renderer::Impl::RunPostFX(const PostParams& p, ITextureView*& bloomOut, ITextureView*& aoOut) {
    bloomOut = nullptr;
    aoOut    = nullptr;
    if (!p.bloom && !p.ssao) return;

    const SwapChainDesc& sc = swapChain->GetDesc();
    if (sc.Width == 0 || sc.Height == 0) return;

    // Prepare the shared context + the enabled effects (each early-outs on unchanged
    // size). The PSOs are what PostFXContext::IsPSOsReady() gates on below.
    PostFXContext::FrameDesc frame;
    frame.Index  = frameIndex;
    frame.Width  = sc.Width;
    frame.Height = sc.Height;
    postFX->PrepareResources(device, frame, PostFXContext::FEATURE_FLAG_NONE);
    if (p.bloom) bloom->PrepareResources(device, context, postFX.get(), Bloom::FEATURE_FLAG_NONE);
    if (p.ssao)  ssao->PrepareResources(device, context, postFX.get(), ScreenSpaceAmbientOcclusion::FEATURE_FLAG_NONE);

    // Run the shared context: real camera + scene depth (as curr and prev), zero
    // motion. This computes the reprojected-depth/blue-noise resources the effects
    // build on and flips IsPSOsReady() true.
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
        bra.pColorBufferSRV = hdrColor->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
        bra.pBloomAttribs   = &attribs;
        bloom->Execute(bra);
        bloomOut = bloom->GetBloomTextureSRV();
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

    // 1x1 white texture = "fully visible" — bound to the resolve's g_AO whenever SSAO
    // is off or not yet ready, so the AO multiply is a no-op without a shader branch.
    const Uint8    white[4] = { 255, 255, 255, 255 };
    TextureSubResData sub{ white, 4 };
    TextureData       texData{ &sub, 1 };
    TextureDesc wd;
    wd.Name      = "AO white default";
    wd.Type      = RESOURCE_DIM_TEX_2D;
    wd.Width     = 1;
    wd.Height    = 1;
    wd.MipLevels = 1;
    wd.Format    = TEX_FORMAT_RGBA8_UNORM;
    wd.BindFlags = BIND_SHADER_RESOURCE;
    m_impl->device->CreateTexture(wd, &texData, &m_impl->aoWhite);

    return m_impl->postFX && m_impl->bloom && m_impl->ssao && m_impl->aoWhite;
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
    m_impl->bloom.reset();            // DiligentFX effect objects own GPU resources
    m_impl->ssao.reset();
    m_impl->postFX.reset();
    m_impl->motionVectors.Release();
    m_impl->aoWhite.Release();
    m_impl->tonemapSRB.Release();
    m_impl->tonemapPSO.Release();
    m_impl->postConstants.Release();
    m_impl->hdrColor.Release();
    m_impl->normalBuffer.Release();
    m_impl->sceneDepth.Release();
    m_impl->fillSRB.Release();
    m_impl->outlineSRB.Release();
    m_impl->fillPSO.Release();
    m_impl->outlinePSO.Release();
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
    //  - bloomSRV: scene + glow already composited (a drop-in HDR resolve input, so
    //    tonemap.hlsl is unchanged), or null -> resolve the raw scene.
    //  - aoSRV: SSAO visibility, or null -> the white "no occlusion" default.
    ITextureView* bloomSRV = nullptr;
    ITextureView* aoSRV    = nullptr;
    m_impl->RunPostFX(m_impl->post, bloomSRV, aoSRV);

    // Resolve inputs: bloom output or raw scene; real AO or the white default.
    ITextureView* postInput = bloomSRV ? bloomSRV : m_impl->hdrColor->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
    ITextureView* aoInput   = aoSRV    ? aoSRV    : m_impl->aoWhite->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);

    // Resolve the HDR source to the back buffer: SSAO darkening + exposure + tone
    // map. Leaves the back-buffer RTV bound so the UI overlay draws on top.
    ITextureView* rtv = m_impl->swapChain->GetCurrentBackBufferRTV();
    m_impl->context->SetRenderTargets(1, &rtv, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    {
        MapHelper<PostConstants> cb(m_impl->context, m_impl->postConstants, MAP_WRITE, MAP_FLAG_DISCARD);
        cb->exposure     = m_impl->post.exposure;
        cb->toneMap      = m_impl->post.toneMap ? 1.0f : 0.0f;
        cb->outputSRGB   = m_impl->outputSRGB ? 1.0f : 0.0f;
        // Only apply strength when a real AO texture was produced (else g_AO is the
        // white default and the multiply is a no-op anyway).
        cb->ssaoStrength = aoSRV ? m_impl->post.ssaoStrength : 0.0f;
    }

    // Point the resolve at this frame's HDR source + AO. Both are dynamic, so they
    // may safely differ from last frame (scene <-> bloom, AO on/off, or a resize).
    if (auto* v = m_impl->tonemapSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_HDRColor"))
        v->Set(postInput);
    if (auto* v = m_impl->tonemapSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_AO"))
        v->Set(aoInput);

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

    // Turntable: rotate the world about the origin, then push it `distance` down
    // +Z in front of the (left-handed) camera at the origin.
    const float4x4 view = float4x4::RotationY(cam.yaw) *
                          float4x4::RotationX(cam.pitch) *
                          float4x4::Translation(0.0f, 0.0f, cam.distance);
    // NegativeOneToOneZ = false -> [0,1] depth range for Vulkan/D3D.
    const float4x4 proj = float4x4::Projection(cam.fovY, aspect, cam.nearZ, cam.farZ, false);

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

void Renderer::DrawMesh(MeshHandle handle, const Transform& t, const Transform& prevT, const Material& mat) {
    const uint32_t idx = static_cast<uint32_t>(handle);
    if (idx == 0 || idx > m_impl->meshes.size())
        return;
    const Impl::GpuMesh& mesh = m_impl->meshes[idx - 1];

    // This frame's and last frame's clip transforms — the pair the motion-vector pass
    // differences. Camera motion rides in via viewProj/prevViewProj; object motion via
    // the two world matrices.
    const float4x4 world   = WorldFromTransform(t);
    const float4x4 wvp     = world * m_impl->viewProj;
    const float4x4 prevWvp = WorldFromTransform(prevT) * m_impl->prevViewProj;

    {
        const float3& L = m_impl->lightDir;
        MapHelper<ShaderConstants> cb(m_impl->context, m_impl->constants, MAP_WRITE, MAP_FLAG_DISCARD);
        cb->worldViewProj     = wvp;
        cb->world             = world;
        cb->prevWorldViewProj = prevWvp;
        cb->lightDir          = float4(L.x, L.y, L.z, 0.0f);
        cb->baseColor         = float4(mat.baseColor.x, mat.baseColor.y, mat.baseColor.z, 1.0f);
        cb->outline           = float4(mat.outlineColor.x, mat.outlineColor.y, mat.outlineColor.z, mat.outlineWidth);
        cb->params            = float4(mat.bands, mat.ambient, 0.0f, 0.0f);
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