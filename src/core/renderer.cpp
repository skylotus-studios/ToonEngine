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
#include "Shader.h"
#include "PipelineState.h"
#include "RefCntAutoPtr.hpp"
#include "MapHelper.hpp"     // Diligent-GraphicsTools
#include "BasicMath.hpp"

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

namespace toon {

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

    // Per-frame scene state.
    float4x4 viewProj = float4x4::Identity();
    float3   lightDir = float3(0.5f, 0.8f, -0.3f);  // world-space dir TO light (shader normalizes)
};

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
    return true;
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

    const SwapChainDesc& sc = m_impl->swapChain->GetDesc();

    // Build a graphics PSO for one toon pass and wire the shared CB into it.
    auto buildPass = [&](const char* name, IShader* vs, IShader* ps, CULL_MODE cull,
                         RefCntAutoPtr<IPipelineState>&         pso,
                         RefCntAutoPtr<IShaderResourceBinding>& srb) -> bool {
        GraphicsPipelineStateCreateInfo ci;
        ci.PSODesc.Name         = name;
        ci.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;

        GraphicsPipelineDesc& gp = ci.GraphicsPipeline;
        gp.NumRenderTargets                     = 1;
        gp.RTVFormats[0]                        = sc.ColorBufferFormat;
        gp.DSVFormat                            = sc.DepthBufferFormat;
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

void Renderer::Shutdown() {
    if (!m_impl) return;
    if (m_impl->context) m_impl->context->Flush();
    ShutdownUI(); // must release ImGui's GPU resources before the device

    // Release scene/pipeline GPU objects before the device.
    m_impl->meshes.clear();
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

void Renderer::BeginFrame(const Color& c) {
    ITextureView* rtv = m_impl->swapChain->GetCurrentBackBufferRTV();
    ITextureView* dsv = m_impl->swapChain->GetDepthBufferDSV();
    m_impl->context->SetRenderTargets(1, &rtv, dsv, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    const float clear[] = { c.r, c.g, c.b, c.a };
    m_impl->context->ClearRenderTarget(rtv, clear, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    m_impl->context->ClearDepthStencil(dsv, CLEAR_DEPTH_FLAG, 1.0f, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
}

void Renderer::EndFrame() {
    m_impl->swapChain->Present(); // vsync on by default
}

void Renderer::Resize(uint32_t width, uint32_t height) {
    if (m_impl->swapChain && width > 0 && height > 0)
        m_impl->swapChain->Resize(width, height);
}

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

bool Renderer::InitUI(GLFWwindow* window) {
    // ImGuiImplDiligent's constructor calls ImGui::CreateContext() — it must
    // run before ImGui_ImplGlfw_InitForVulkan(), which itself calls
    // ImGui::GetIO() and asserts if no context exists yet.
    ImGuiDiligentCreateInfo imguiCI{ m_impl->device, m_impl->swapChain->GetDesc() };
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
    m_impl->imgui.reset(); // destructor invalidates GPU objects + destroys the ImGui context
    ImGui_ImplGlfw_Shutdown();
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
