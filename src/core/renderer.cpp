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

// DiligentFX post-processing: the Bloom effect and the shared PostFXContext it
// depends on (roadmap #1). Both compile into the DiligentFX target, whose root is
// on the include path, so these resolve short-form.
#include "PostFXContext.hpp"
#include "Bloom.hpp"

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
}} // namespace Diligent::HLSL

namespace toon {

// --- Internal formats, GPU-mirror types & PIMPL state -----------------------

// The scene renders to an HDR target, then a full-screen pass tone-maps it to
// the back buffer — the foundation for DiligentFX post effects.
static constexpr TEXTURE_FORMAT kHDRFormat        = TEX_FORMAT_RGBA16_FLOAT;
static constexpr TEXTURE_FORMAT kSceneDepthFormat = TEX_FORMAT_D32_FLOAT;

// GPU mirror of the toon_common.hlsli cbuffer. Field order/size MUST match it
// (two row-major float4x4 rows + four float4 rows = 192 bytes, 16-aligned).
struct ShaderConstants {
    float4x4 worldViewProj;
    float4x4 world;
    float4   lightDir;
    float4   baseColor;
    float4   outline;      // rgb color, w = extrude width
    float4   params;       // x = bands, y = ambient
};

// GPU mirror of tonemap.hlsl's PostConstants.
struct PostConstants {
    float exposure;
    float toneMap;      // 1 = ACES
    float outputSRGB;   // 1 = encode sRGB in-shader
    float pad;
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
    RefCntAutoPtr<ITexture>               sceneDepth;    // D32 depth (also SRV for PostFX)
    RefCntAutoPtr<IPipelineState>         tonemapPSO;
    RefCntAutoPtr<IShaderResourceBinding> tonemapSRB;
    RefCntAutoPtr<IBuffer>                postConstants;
    bool                                  outputSRGB = false;  // back buffer is a non-sRGB UNORM

    // DiligentFX Bloom (roadmap #1). Bloom needs a PostFXContext, which itself
    // requires depth + motion-vector inputs and camera attribs to reach its
    // "PSOs ready" state (the gate Bloom checks). Bloom reads none of that — it's
    // shared plumbing for temporal effects — so we feed the scene depth (as both
    // current and previous; we keep no history), a zero motion-vector target, and
    // a zeroed camera purely to satisfy PostFXContext::Execute.
    std::unique_ptr<PostFXContext> postFX;
    std::unique_ptr<Bloom>         bloom;
    RefCntAutoPtr<ITexture>        motionVectors;   // RG16F, cleared to zero (unused by bloom)
    HLSL::CameraAttribs            postCamera{};    // zeroed (unused by bloom)
    Uint32                         frameIndex = 0;

    // Runs the bloom chain for this frame and returns the SRV of the composited
    // scene+bloom HDR texture (a drop-in for the tone-map input), or nullptr if the
    // effect isn't ready yet (shaders still compiling, or a zero-size target).
    ITextureView* RunBloom(const PostParams& p);

    // Per-frame scene state.
    float4x4   viewProj = float4x4::Identity();
    float3     lightDir = float3(0.5f, 0.8f, -0.3f);  // world-space dir TO light (shader normalizes)
    PostParams post;
};

// Bloom for this frame: prepare + execute PostFXContext (only to flip its ready
// gate), then Bloom over the HDR scene color. See the Impl field comments above for
// why PostFXContext gets depth/motion/camera it never reads.
ITextureView* Renderer::Impl::RunBloom(const PostParams& p) {
    const SwapChainDesc& sc = swapChain->GetDesc();
    if (sc.Width == 0 || sc.Height == 0) return nullptr;

    PostFXContext::FrameDesc frame;
    frame.Index  = frameIndex;
    frame.Width  = sc.Width;
    frame.Height = sc.Height;
    postFX->PrepareResources(device, frame, PostFXContext::FEATURE_FLAG_NONE);
    bloom->PrepareResources(device, context, postFX.get(), Bloom::FEATURE_FLAG_NONE);

    ITextureView* depthSRV  = sceneDepth->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
    ITextureView* motionSRV = motionVectors->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
    postCamera.f4ViewportSize = float4(static_cast<float>(sc.Width), static_cast<float>(sc.Height),
                                       1.0f / sc.Width, 1.0f / sc.Height);
    postCamera.uiFrameIndex = frameIndex;

    PostFXContext::RenderAttributes pfx;
    pfx.pDevice             = device;
    pfx.pDeviceContext      = context;
    pfx.pCurrDepthBufferSRV = depthSRV;
    pfx.pPrevDepthBufferSRV = depthSRV;   // no history — reuse current (unused by bloom)
    pfx.pMotionVectorsSRV   = motionSRV;
    pfx.pCurrCamera         = &postCamera;
    pfx.pPrevCamera         = &postCamera;
    postFX->Execute(pfx);

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

    ++frameIndex;

    // Until the PSOs finish compiling, Bloom emits a black placeholder — fall back
    // to the raw scene so the first frame(s) aren't black.
    if (!postFX->IsPSOsReady()) return nullptr;
    return bloom->GetBloomTextureSRV();
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

    if (!CreateBloom()) {
        std::fprintf(stderr, "Renderer: failed to create bloom effect\n");
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

    // Zero motion-vector target for PostFXContext. The toon scene has no motion and
    // Bloom ignores the result, but PostFXContext requires a motion input to run;
    // clear it once (we never write it again) so nothing reads uninitialized data.
    TextureDesc md;
    md.Name      = "motion vectors (zero)";
    md.Type      = RESOURCE_DIM_TEX_2D;
    md.Width     = width;
    md.Height    = height;
    md.MipLevels = 1;
    md.Format    = TEX_FORMAT_RG16_FLOAT;
    md.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;
    m_impl->device->CreateTexture(md, nullptr, &m_impl->motionVectors);
    if (m_impl->motionVectors) {
        ITextureView* mrtv = m_impl->motionVectors->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
        const float   zero[] = { 0.0f, 0.0f, 0.0f, 0.0f };
        m_impl->context->SetRenderTargets(1, &mrtv, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        m_impl->context->ClearRenderTarget(mrtv, zero, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    }

    return m_impl->hdrColor && m_impl->sceneDepth && m_impl->motionVectors;
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
    ShaderResourceVariableDesc vars[] = {
        { SHADER_TYPE_PIXEL, "g_HDRColor",    SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC },
        { SHADER_TYPE_PIXEL, "PostConstants", SHADER_RESOURCE_VARIABLE_TYPE_STATIC  },
    };
    ci.PSODesc.ResourceLayout.Variables    = vars;
    ci.PSODesc.ResourceLayout.NumVariables = sizeof(vars) / sizeof(vars[0]);

    SamplerDesc linClamp;
    linClamp.MinFilter = FILTER_TYPE_LINEAR; linClamp.MagFilter = FILTER_TYPE_LINEAR; linClamp.MipFilter = FILTER_TYPE_LINEAR;
    linClamp.AddressU  = TEXTURE_ADDRESS_CLAMP; linClamp.AddressV = TEXTURE_ADDRESS_CLAMP; linClamp.AddressW = TEXTURE_ADDRESS_CLAMP;
    ImmutableSamplerDesc immSamplers[] = { { SHADER_TYPE_PIXEL, "g_HDRColor", linClamp } };
    ci.PSODesc.ResourceLayout.ImmutableSamplers    = immSamplers;
    ci.PSODesc.ResourceLayout.NumImmutableSamplers = sizeof(immSamplers) / sizeof(immSamplers[0]);

    device->CreateGraphicsPipelineState(ci, &m_impl->tonemapPSO);
    if (!m_impl->tonemapPSO) return false;
    if (auto* v = m_impl->tonemapPSO->GetStaticVariableByName(SHADER_TYPE_PIXEL, "PostConstants"))
        v->Set(m_impl->postConstants);
    m_impl->tonemapPSO->CreateShaderResourceBinding(&m_impl->tonemapSRB, true);
    return m_impl->tonemapSRB != nullptr;
}

bool Renderer::CreateBloom() {
    // Sync PSO creation: the first EndScene blocks briefly to compile the effect's
    // shaders, so Bloom is live from the first frame it runs (no async black-frames
    // to special-case). RunBloom still falls back to the raw scene if not ready.
    PostFXContext::CreateInfo pfxCI;
    m_impl->postFX = std::make_unique<PostFXContext>(m_impl->device, pfxCI);

    Bloom::CreateInfo bloomCI;
    m_impl->bloom = std::make_unique<Bloom>(m_impl->device, bloomCI);

    return m_impl->postFX && m_impl->bloom;
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
        gp.NumRenderTargets                     = 1;
        gp.RTVFormats[0]                        = kHDRFormat;          // scene renders to the HDR target
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
    m_impl->postFX.reset();
    m_impl->motionVectors.Release();
    m_impl->tonemapSRB.Release();
    m_impl->tonemapPSO.Release();
    m_impl->postConstants.Release();
    m_impl->hdrColor.Release();
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
    // The scene renders into the HDR offscreen target (resolved in EndScene).
    ITextureView* rtv = m_impl->hdrColor->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
    ITextureView* dsv = m_impl->sceneDepth->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL);
    m_impl->context->SetRenderTargets(1, &rtv, dsv, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    const float clear[] = { c.r, c.g, c.b, c.a };
    m_impl->context->ClearRenderTarget(rtv, clear, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    m_impl->context->ClearDepthStencil(dsv, CLEAR_DEPTH_FLAG, 1.0f, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
}

void Renderer::SetPostParams(const PostParams& params) {
    m_impl->post = params;
}

void Renderer::EndScene() {
    // Pick the HDR source to resolve. By default it's the raw scene target; with
    // bloom on (and ready) it's Bloom's output, which already holds scene + glow
    // composited (see Bloom_ComputeUpsampledTexture.fx) — a drop-in HDR input, so
    // the tone-map shader is unchanged. RunBloom binds its own render targets, so
    // this must run before we bind the back buffer below.
    ITextureView* postInput = m_impl->hdrColor->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
    if (m_impl->post.bloom) {
        if (ITextureView* bloomSRV = m_impl->RunBloom(m_impl->post))
            postInput = bloomSRV;
    }

    // Resolve the HDR source to the back buffer: exposure + tone map. Leaves the
    // back-buffer RTV bound so the UI overlay draws on top.
    ITextureView* rtv = m_impl->swapChain->GetCurrentBackBufferRTV();
    m_impl->context->SetRenderTargets(1, &rtv, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    {
        MapHelper<PostConstants> cb(m_impl->context, m_impl->postConstants, MAP_WRITE, MAP_FLAG_DISCARD);
        cb->exposure   = m_impl->post.exposure;
        cb->toneMap    = m_impl->post.toneMap ? 1.0f : 0.0f;
        cb->outputSRGB = m_impl->outputSRGB ? 1.0f : 0.0f;
        cb->pad        = 0.0f;
    }

    // Point the resolve at this frame's HDR source. g_HDRColor is dynamic, so this
    // may safely differ from last frame (scene <-> bloom, or a resized target).
    if (auto* v = m_impl->tonemapSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_HDRColor"))
        v->Set(postInput);

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
    m_impl->viewProj = view * proj;
}

void Renderer::SetLight(const Vec3& directionToLight) {
    m_impl->lightDir = float3(directionToLight.x, directionToLight.y, directionToLight.z);
}

void Renderer::DrawMesh(MeshHandle handle, const Transform& t, const Material& mat) {
    const uint32_t idx = static_cast<uint32_t>(handle);
    if (idx == 0 || idx > m_impl->meshes.size())
        return;
    const Impl::GpuMesh& mesh = m_impl->meshes[idx - 1];

    // World, then world-view-proj (Diligent is row-major / row-vector: v * M).
    const float4x4 world = float4x4::Scale(t.scale.x, t.scale.y, t.scale.z) *
                           float4x4::RotationX(t.rotationEuler.x) *
                           float4x4::RotationY(t.rotationEuler.y) *
                           float4x4::RotationZ(t.rotationEuler.z) *
                           float4x4::Translation(t.position.x, t.position.y, t.position.z);
    const float4x4 wvp = world * m_impl->viewProj;

    {
        const float3& L = m_impl->lightDir;
        MapHelper<ShaderConstants> cb(m_impl->context, m_impl->constants, MAP_WRITE, MAP_FLAG_DISCARD);
        cb->worldViewProj = wvp;
        cb->world         = world;
        cb->lightDir      = float4(L.x, L.y, L.z, 0.0f);
        cb->baseColor     = float4(mat.baseColor.x, mat.baseColor.y, mat.baseColor.z, 1.0f);
        cb->outline       = float4(mat.outlineColor.x, mat.outlineColor.y, mat.outlineColor.z, mat.outlineWidth);
        cb->params        = float4(mat.bands, mat.ambient, 0.0f, 0.0f);
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