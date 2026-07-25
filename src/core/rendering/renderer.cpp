//============================================================================
//  core/renderer.cpp: Diligent Engine (Vulkan) implementation of the seam.
//
//  Everything Diligent-specific is contained in this translation unit. The
//  public header (renderer.h) exposes only opaque handles and plain types.
//============================================================================
#include "core/rendering/renderer.h"

#include "core/platform/paths.h" // Assets::Shaders (exe-relative shader source dir)

// GLFW with native access: extracting the OS window handle is a backend
// concern, so it lives behind the seam here. main.cpp includes only plain GLFW.
// GLFW_INCLUDE_NONE is set engine-wide (CMakeLists.txt), not per-file here, since
// core/input/input_device.h also needs it ahead of any single TU's own #define.
#include <GLFW/glfw3.h>
#if defined(_WIN32)
#define GLFW_EXPOSE_NATIVE_WIN32
#elif defined(__linux__)
#define GLFW_EXPOSE_NATIVE_X11
#endif
#if defined(_WIN32) || defined(__linux__)
#include <GLFW/glfw3native.h>
#endif

// Title-bar theming (SetTitleBarTheme, below) via DWM. Older Windows SDKs don't declare
// these DWMWINDOWATTRIBUTE values even though the OS itself has supported them since
// Windows 10 1809 (dark mode) / Windows 11 22H2 (caption/text color) -- define the
// numeric values locally so the build doesn't depend on SDK version. DwmSetWindowAttribute
// takes a plain DWORD, so this is safe whether or not the SDK's own enum already has them.
#if defined(_WIN32)
#include <dwmapi.h>
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_CAPTION_COLOR
#define DWMWA_CAPTION_COLOR 35
#endif
#ifndef DWMWA_TEXT_COLOR
#define DWMWA_TEXT_COLOR 36
#endif
#endif

#include "EngineFactoryVk.h"
#include "RenderDevice.h"
#include "DeviceContext.h"
#include "SwapChain.h"
#include "GraphicsTypes.h"
#include "Buffer.h"
#include "Texture.h"
#include "TextureUtilities.h" // CreateTextureFromFile: asset-browser thumbnails/previews
#include "Sampler.h"
#include "Shader.h"
#include "PipelineState.h"
#include "RefCntAutoPtr.hpp"
#include "MapHelper.hpp" // Diligent-GraphicsTools
#include "BasicMath.hpp"

// Roadmap #10 (shader hot-reload): IRenderStateCache wraps CreateShader/CreateGraphicsPipelineState
// so Reload() can recompile whatever .hlsl source changed; LoadAndGetArchiverFactory() is the
// runtime-loaded factory it needs at creation (see Renderer::Init). Both always compile in
// (the cache exists in every build, see CreateRenderStateCache below), even though hot-reload
// itself -- the efsw watcher, EnableHotReload, and the Settings-panel button -- is Debug-only
// (TOON_SHADER_HOT_RELOAD, set in CMakeLists.txt).
#include "RenderStateCache.h"
#include "ArchiverFactoryLoader.h"

#ifdef TOON_SHADER_HOT_RELOAD
// efsw is a small cross-platform (Windows/Linux/macOS) file-system watcher (see CMakeLists.txt's
// efsw add_subdirectory guard) -- notices a changed .hlsl file and flips shadersDirty, checked
// once per frame in BeginFrame. Not part of the Diligent seam; kept behind this ifdef and this
// TU only, same "third-party type stays out of renderer.h" rule as every Diligent type here.
#include <efsw/efsw.hpp>
#endif

// DiligentFX post-processing: the Bloom + SSAO effects and the shared PostFXContext
// they depend on. All compile into the DiligentFX target, whose root is on the
// include path, so these resolve short-form.
#include "PostFXContext.hpp"
#include "Bloom.hpp"
#include "ScreenSpaceAmbientOcclusion.hpp"
#include "DepthOfField.hpp"
#include "TemporalAntiAliasing.hpp"
#include "ScreenSpaceReflection.hpp"

// Cascaded shadow maps: DiligentFX's ShadowMapManager component (see the #include further
// down, after the HLSL mirror-struct block below -- ShadowMapManager.hpp does its own
// "namespace Diligent { #include BasicStructures.fxh ... }" internally, unnested, unlike
// this file's namespace Diligent::HLSL wrapper that PostFXContext.hpp/Bloom.hpp/etc. all
// expect; BasicStructures.fxh's include guard means whichever inclusion runs FIRST wins for
// this whole translation unit, so ShadowMapManager.hpp must come after ours, not before).
// ShaderSourceFactoryUtils' CreateCompoundShaderSourceFactory + DiligentFXShaderSourceStreamFactory
// let our own toon shaders #include DiligentFX's shared Shadows.fxh/BasicStructures.fxh (see
// Renderer::Init) -- those live embedded in the DiligentFX lib, not under assets/shaders, so
// our own shader factory alone can't see them.
#include "ShaderSourceFactoryUtils.hpp"
// Utilities/ doesn't add its own public include dir (unlike Components/, PostProcess/*),
// only the DiligentFX root does (target_include_directories(DiligentFX PUBLIC .) in its
// top-level CMakeLists.txt) -- so this one needs the path relative to that root.
#include "Utilities/interface/DiligentFXShaderSourceStreamFactory.hpp"

// DiligentTools asset loading: the glTF/GLB loader (Diligent::GLTF::Model owns the
// vertex/index buffers + textures). Self-contained in DiligentTools, no DiligentFX /
// PBR renderer needed (we cel-shade with our own PSO).
#include "GLTFLoader.hpp"

// DiligentTools' CPU-side image decoder (PNG/JPEG/TGA/...), used to load the window
// icon. No GPU device involved, so this needs no Renderer::Impl state.
#include "Image.h"

// Dear ImGui + Diligent's ImGui renderer backend (DiligentTools). The GLFW
// platform backend (imgui_impl_glfw.cpp) is compiled directly into ToonEngine
// by CMakeLists.txt, since DiligentTools doesn't ship a GLFW backend itself.
#include "imgui.h"
#include "ImGuiImplDiligent.hpp"
#include "backends/imgui_impl_glfw.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace Diligent;

// DiligentFX's effect-parameter structs, compiled as C++ from the same headers its
// shaders include. BasicStructures.fxh brings in CameraAttribs (which PostFXContext
// wants); BloomStructures.fxh defines BloomAttribs. float4x4/float4/uint etc.
// resolve to the Diligent:: aliases pulled in by BasicMath.hpp above. (Mirrors how
// DiligentFX's own .cpp files include these; see Bloom.cpp.) BasicStructures.fxh
// does a bare #include "ShaderDefinitions.fxh"; CMake puts that directory on the
// include path so it resolves.
namespace Diligent {
    namespace HLSL {
#include "Shaders/Common/public/BasicStructures.fxh"
#include "Shaders/PostProcess/Bloom/public/BloomStructures.fxh"
#include "Shaders/PostProcess/ScreenSpaceAmbientOcclusion/public/ScreenSpaceAmbientOcclusionStructures.fxh"
#include "Shaders/PostProcess/DepthOfField/public/DepthOfFieldStructures.fxh"
#include "Shaders/PostProcess/TemporalAntiAliasing/public/TemporalAntiAliasingStructures.fxh"
#include "Shaders/PostProcess/ScreenSpaceReflection/public/ScreenSpaceReflectionStructures.fxh"
#include "Shaders/Common/public/CoordinateGridStructures.fxh"
    } // namespace HLSL
} // namespace Diligent

// ShadowMapManager.hpp/.cpp (external/DiligentFX/Components) wrap BasicStructures.fxh in
// plain "namespace Diligent { ... }", unnested -- unlike the PostProcess/Common family
// above, which all forward-declare/expect Diligent::HLSL::CameraAttribs (see PostFXContext.hpp).
// Both conventions are needed in this one translation unit, but BasicStructures.fxh's own
// #include guard makes a second, unmodified #include a no-op: a using-directive bridge
// isn't enough here (tried first) -- ShadowMapManager.cpp is a SEPARATE translation unit
// that always resolves ShadowMapAttribs to bare Diligent::ShadowMapAttribs when IT compiles
// DistributeCascades, so our call site must match that exact (mangled) type, not merely be
// able to *find* the nested one under a different name. Force a second, independent
// expansion of the header at bare Diligent:: scope via #undef, so our declaration and the
// library's actual compiled symbol agree.
#undef _BASIC_STRUCTURES_FXH_
namespace Diligent {
#include "Shaders/Common/public/BasicStructures.fxh"
} // namespace Diligent

#include "ShadowMapManager.hpp"

// Roadmap #12 (grid + sky gradient backdrop): DiligentFX's ready-made coordinate-grid
// renderer (Components, same library as ShadowMapManager above) -- an infinite ground grid
// with antialiased/multi-LOD lines and colored axes, built on the shared HLSL::CameraAttribs
// this file already fills (FillCameraAttribs). Its own .cpp includes BasicStructures.fxh the
// same nested "namespace Diligent { namespace HLSL { ... } }" way the PostFX family above
// does, unlike ShadowMapManager's bare-namespace form above -- so it doesn't need the #undef
// workaround that ShadowMapManager required.
#include "CoordinateGridRenderer.hpp"

namespace toon {

    // Sets the OS window/taskbar icon from an image file, via DiligentTools' CPU-side
    // decoder (Image.h, no GPU device needed). GLFW wants tightly-packed 8-bit RGBA
    // rows; the decoder's RowStride may be padded and NumComponents may be less than 4
    // (grayscale/RGB/etc.), so this expands each source pixel into the RGBA buffer GLFW
    // copies internally (glfwSetWindowIcon doesn't retain `pixels` after it returns).
    bool SetWindowIcon(GLFWwindow *window, const char *path) {
        if (!window || !path) { return false; }

        RefCntAutoPtr<Image> image;
        const IMAGE_FILE_FORMAT format = CreateImageFromFile(path, &image);
        if (format == IMAGE_FILE_FORMAT_UNKNOWN || !image) {
            std::fprintf(stderr, "Renderer: failed to load window icon '%s'\n", path);
            return false;
        }

        const ImageDesc &desc = image->GetDesc();
        if (desc.ComponentType != VT_UINT8 || desc.NumComponents == 0 || desc.NumComponents > 4) {
            std::fprintf(stderr, "Renderer: window icon '%s' has an unsupported pixel format\n", path);
            return false;
        }

        const auto *src = image->GetData()->GetConstDataPtr<uint8_t>();
        std::vector<uint8_t> rgba(static_cast<size_t>(desc.Width) * desc.Height * 4);
        for (Uint32 y = 0; y < desc.Height; ++y) {
            const uint8_t *row = src + static_cast<size_t>(y) * desc.RowStride;
            for (Uint32 x = 0; x < desc.Width; ++x) {
                const uint8_t *px = row + static_cast<size_t>(x) * desc.NumComponents;
                uint8_t *dst = &rgba[(static_cast<size_t>(y) * desc.Width + x) * 4];
                switch (desc.NumComponents) {
                    case 1: // grayscale
                        dst[0] = dst[1] = dst[2] = px[0];
                        dst[3] = 255;
                        break;
                    case 2: // grayscale + alpha
                        dst[0] = dst[1] = dst[2] = px[0];
                        dst[3] = px[1];
                        break;
                    case 3: // RGB
                        dst[0] = px[0];
                        dst[1] = px[1];
                        dst[2] = px[2];
                        dst[3] = 255;
                        break;
                    default: // RGBA
                        dst[0] = px[0];
                        dst[1] = px[1];
                        dst[2] = px[2];
                        dst[3] = px[3];
                        break;
                }
            }
        }

        GLFWimage icon;
        icon.width = static_cast<int>(desc.Width);
        icon.height = static_cast<int>(desc.Height);
        icon.pixels = rgba.data();
        glfwSetWindowIcon(window, 1, &icon);
        return true;
    }

    // Themes the native title bar to match the ImGui editor (see renderer.h) -- otherwise
    // it stays the OS's stock white/light bar, clashing with every one of the editor's dark
    // themes. The dark-mode flag (window frame + system button glyphs) works from Windows 10
    // 1809 on; the exact caption/text color needs Windows 11 22H2's DWMWA_CAPTION_COLOR/
    // TEXT_COLOR and is simply ignored (DwmSetWindowAttribute fails, nothing thrown) on older
    // Windows, which then keeps the OS's own dark-mode gray instead of stock white.
    bool SetTitleBarTheme(GLFWwindow *window, Color background, Color text) {
#if defined(_WIN32)
        if (!window) { return false; }
        const HWND hwnd = glfwGetWin32Window(window);
        if (!hwnd) { return false; }

        const BOOL useDarkMode = TRUE;
        DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &useDarkMode, sizeof(useDarkMode));

        auto toByte = [](float v) {
            if (v < 0.0f) { v = 0.0f; }
            if (v > 1.0f) { v = 1.0f; }
            return static_cast<BYTE>(v * 255.0f + 0.5f);
        };
        const COLORREF captionColor = RGB(toByte(background.r), toByte(background.g), toByte(background.b));
        const COLORREF textColor = RGB(toByte(text.r), toByte(text.g), toByte(text.b));
        DwmSetWindowAttribute(hwnd, DWMWA_CAPTION_COLOR, &captionColor, sizeof(captionColor));
        DwmSetWindowAttribute(hwnd, DWMWA_TEXT_COLOR, &textColor, sizeof(textColor));
        return true;
#else
        (void) window;
        (void) background;
        (void) text;
        return false;
#endif
    }

    // --- Internal formats, GPU-mirror types & PIMPL state -----------------------

    // The scene renders to an HDR color target + a world-space normal G-buffer (for
    // SSAO), then a full-screen pass tone-maps to the back buffer. RGBA16F holds signed
    // normals in [-1,1] directly, so no encode/decode is needed.
    static constexpr TEXTURE_FORMAT kHDRFormat = TEX_FORMAT_RGBA16_FLOAT;
    static constexpr TEXTURE_FORMAT kNormalFormat = TEX_FORMAT_RGBA16_FLOAT;
    static constexpr TEXTURE_FORMAT kMotionFormat = TEX_FORMAT_RG16_FLOAT; // NDC motion (SSAO temporal/DoF)
    static constexpr TEXTURE_FORMAT kSceneDepthFormat = TEX_FORMAT_D32_FLOAT;

    // Cascaded shadow maps: fixed at Init (unlike the window-sized offscreen targets, the
    // shadow map atlas doesn't need to resize with the swap chain).
    static constexpr Uint32 kShadowCascades = 4;
    static constexpr Uint32 kShadowResolution = 2048;

    // Debug wireframe overlay (M2.1): a fixed-capacity dynamic vertex buffer, remapped per
    // DrawWireframe call. Comfortably above any ColliderWireframe shape's point count
    // (Capsule, the largest, is ~200) with headroom to spare.
    static constexpr Uint32 kMaxWireframeVertices = 1024;

    // In-game UI overlay (roadmap #17): a fixed-capacity dynamic vertex buffer remapped per
    // DrawUI call, holding one frame's tessellated UI (6 verts per quad). 65536 verts is
    // ~10.9k quads, comfortably above a dense HUD + menu's glyph + panel count; DrawUI clamps
    // and warns past it, the same as DrawWireframe.
    static constexpr Uint32 kMaxUIVertices = 65536;

    // GPU mirror of the toon_common.hlsli cbuffer. Field order/size MUST match it
    // (five row-major float4x4 rows + five float4 rows = 400 bytes, 16-aligned).
    struct ShaderConstants {
        float4x4 worldViewProj;
        float4x4 world;
        float4x4 normalMatrix;      // inverse-transpose of world (correct normals under non-uniform scale)
        float4x4 prevWorldViewProj; // previous frame, for motion vectors
        float4x4 prevNormalMatrix;  // inverse-transpose of the PREVIOUS frame's world (outline motion)
        float4 lightDir;
        float4 lightColor; // rgb = light color * intensity, premultiplied; w unused
        float4 baseColor;
        float4 outline; // rgb color, w = extrude width
        float4 params;  // x = bands, y = ambient, z = roughness
    };

    // GPU mirror of tonemap.hlsl's PostConstants.
    struct PostConstants {
        float exposure;
        float toneMap;      // 1 = ACES
        float outputSRGB;   // 1 = encode sRGB in-shader
        float ssaoStrength; // 0 = AO ignored, 1 = full occlusion
        float ssrStrength;  // reflection add strength
        float pad0, pad1, pad2;
    };

    // GPU mirror of wireframe.hlsl's cbuffer (M2.1's collider debug overlay).
    struct WireframeConstants {
        float4x4 worldViewProj;
        float4 color;
    };

    // GPU mirror of sky_gradient.hlsl's SkyConstants cbuffer (roadmap #12: editor backdrop).
    struct SkyConstants {
        float4x4 invViewProj;
        float4 skyTop;
        float4 skyBottom;
    };

    // GPU mirror of sprite.hlsl's Constants cbuffer (roadmap #13: 2D & sprites).
    struct SpriteConstants {
        float4x4 worldViewProj;
        float4x4 prevWorldViewProj; // previous frame, for the sprite's own motion vectors
        float4x4 world;             // for the quad's world-space normal (G-buffer)
        float4 tint;
        float4 uvRect; // xy = offset, zw = scale
    };

    // GPU mirror of ui.hlsl's Constants cbuffer (roadmap #17: in-game UI). screenSize maps
    // pixel positions to NDC in the VS; atlasSize + pixelRange drive the MSDF median's
    // screen-space edge anti-aliasing in the PS. 32 bytes: two float2 pack into one 16-byte
    // row, pixelRange + pad fill the next, matching the .hlsl cbuffer's layout.
    struct UIConstants {
        float2 screenSize;
        float2 atlasSize;
        float pixelRange;
        float pad0, pad1, pad2;
    };

    // GPU mirror of {model_fill,model_outline}_skinned.hlsl's SkinConstants cbuffer
    // (roadmap #11: skeletal animation). Padded to 16 bytes (a uniform buffer's minimum
    // alignment) even though only the first field is used.
    struct SkinConstants {
        uint32_t jointCount;
        uint32_t pad[3];
    };

#ifdef TOON_SHADER_HOT_RELOAD
    // Fires on efsw's own watch thread (see Renderer::Impl::shaderWatcher below), so it does
    // the absolute minimum: flip a flag. `dirty` outlives the listener (it's an Impl member;
    // the listener is destroyed by ~Impl before `dirty` itself is), and efsw stops calling in
    // once the FileWatcher that owns this listener is destroyed, so no dangling-pointer window.
    class ShaderReloadListener final : public efsw::FileWatchListener {
    public:
        explicit ShaderReloadListener(std::atomic<bool> &dirty) : m_dirty(dirty) {}

        // Note: efsw 1.5.0's oldFilename param is by-value (std::string), not const&, unlike
        // efsw's current master branch -- matched exactly here, since a mismatched parameter
        // type silently turns this into a non-overriding hide of the pure virtual base
        // instead of a real override (caught by the compiler's own -Woverride diagnostic).
        void handleFileAction(efsw::WatchID, const std::string &, const std::string &filename, efsw::Action action,
                              std::string = "") override {
            // Modified: a plain in-place write. Add/Moved: many editors (and this repo's own
            // tooling, confirmed directly by watching a real save) write atomically instead --
            // a temp file, then a rename over the original -- which the OS reports as the old
            // name disappearing and the new content arriving under the original name again, not
            // as a Modified event. React to all three; Delete alone is deliberately excluded
            // (nothing to reload from a file that's gone, e.g. mid-rename it briefly vanishes).
            if (action != efsw::Actions::Modified && action != efsw::Actions::Add &&
                action != efsw::Actions::Moved) {
                return;
            }
            const auto endsWith = [](const std::string &s, const std::string &suffix) {
                return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
            };
            if (endsWith(filename, ".hlsl") || endsWith(filename, ".hlsli")) {
                m_dirty.store(true, std::memory_order_relaxed);
            }
        }

    private:
        std::atomic<bool> &m_dirty;
    };
#endif

    // All Diligent state hides here, behind the PIMPL boundary.
    struct Renderer::Impl {
        RefCntAutoPtr<IRenderDevice> device;
        RefCntAutoPtr<IDeviceContext> context;
        RefCntAutoPtr<ISwapChain> swapChain;
        std::unique_ptr<ImGuiImplDiligent> imgui;

        // Toon pipeline.
        RefCntAutoPtr<IShaderSourceInputStreamFactory> shaderFactory;

        // Roadmap #10 (shader hot-reload): every shader/PSO in this file is created through
        // this cache (CreateToonShader, every CreateGraphicsPipelineState call) instead of
        // `device` directly, in every build -- so no call site needs to branch on whether
        // hot-reload is actually enabled. Only EnableHotReload (Init) and the watcher below
        // differ between Debug and Release.
        RefCntAutoPtr<IRenderStateCache> stateCache;
#ifdef TOON_SHADER_HOT_RELOAD
        // Set (relaxed -- a bool flag, not a value other code depends on being immediately
        // visible) by ShaderReloadListener::handleFileAction on efsw's own watch thread;
        // BeginFrame is the one reader, on the main thread, once per frame.
        std::atomic<bool> shadersDirty{false};
        efsw::FileWatcher shaderWatcher;
        std::unique_ptr<efsw::FileWatchListener> shaderListener;
#endif

        RefCntAutoPtr<IPipelineState> fillPSO;
        RefCntAutoPtr<IPipelineState> outlinePSO;
        RefCntAutoPtr<IShaderResourceBinding> fillSRB;
        RefCntAutoPtr<IShaderResourceBinding> outlineSRB;
        RefCntAutoPtr<IBuffer> constants;

        struct GpuMesh {
            RefCntAutoPtr<IBuffer> vertexBuffer;
            RefCntAutoPtr<IBuffer> indexBuffer;
            Uint32 indexCount = 0;
            // Local-space (object-space) bounds, min/max swept over `vertices[i].position` at
            // creation; mouse-pick (app/picking.cpp) transforms these by an entity's worldMatrix.
            Vec3 boundsMin;
            Vec3 boundsMax;
        };
        std::vector<GpuMesh> meshes; // handle N -> meshes[N-1]; handle 0 = Invalid

        // glTF models (DiligentTools loader). Each GLTF::Model owns its GPU vertex/index
        // buffers + textures; the cel-fill PSO + SRB draw them. handle N -> models[N-1].
        RefCntAutoPtr<IPipelineState> modelPSO; // textured cel fill
        RefCntAutoPtr<IShaderResourceBinding> modelSRB;
        RefCntAutoPtr<IPipelineState> modelOutlinePSO; // inverted-hull outline
        RefCntAutoPtr<IShaderResourceBinding> modelOutlineSRB;

        // Roadmap #11 (skeletal animation): animated-model counterparts of the two PSOs
        // above, plus the shadow one below -- separate PSOs/shaders (not a runtime branch
        // in the static ones) because their vertex layout genuinely differs (JOINTS_0/
        // WEIGHTS_0 in a second buffer slot; see ModelVertexAttribs), the same "different
        // vertex shape needs its own PSO" reason the procedural-vs-model shadow PSOs are
        // already split. Chosen per node (GLTF::Node::pSkin != nullptr), not per model, so
        // a model mixing skinned and unskinned meshes still draws each node correctly.
        RefCntAutoPtr<IPipelineState> modelSkinnedPSO;
        RefCntAutoPtr<IShaderResourceBinding> modelSkinnedSRB;
        RefCntAutoPtr<IPipelineState> modelOutlineSkinnedPSO;
        RefCntAutoPtr<IShaderResourceBinding> modelOutlineSkinnedSRB;

        // Per-draw scalar telling the skinned VS/PS how many joints one frame's worth of
        // jointsBuffer holds (fill/outline only -- the previous frame's palette starts
        // right after, at jointsBuffer[jointCount + i]; the shadow pass has no motion
        // vectors, so it never needs this). Same MapHelper-per-draw idiom as `constants`.
        RefCntAutoPtr<IBuffer> skinConstants;

        // Growable scratch palette for whichever skinned draw is in flight: a structured
        // buffer (not a fixed-size uniform array) so no joint-count ceiling is baked into
        // the skinned shaders. Grown (never shrunk) on demand by EnsureJointsBufferCapacity;
        // jointsBufferCapacity tracks its current size in float4x4 elements.
        RefCntAutoPtr<IBuffer> jointsBuffer;
        Uint32 jointsBufferCapacity = 0;

        std::vector<std::unique_ptr<GLTF::Model>> models;
        // Local-space bounds (model-space, RootTransform = identity), index-matched with
        // `models` (one push_back site, LoadModel, keeps them in sync). Same mouse-pick use as
        // GpuMesh::boundsMin/Max above.
        std::vector<std::pair<Vec3, Vec3>> modelBounds;

        // Editor-UI textures (asset browser thumbnails/previews); decoded image files,
        // unrelated to the toon draw path. handle N -> textures[N-1], same 1-based
        // convention as meshes/models above; a null slot is a destroyed handle.
        std::vector<RefCntAutoPtr<ITexture>> textures;

        // HDR offscreen scene target + tone-map resolve to the back buffer.
        RefCntAutoPtr<ITexture> hdrColor;       // RGBA16F scene color
        RefCntAutoPtr<ITexture> normalBuffer;   // RGBA16F world-space normals (SSAO G-buffer)
        RefCntAutoPtr<ITexture> sceneDepth;     // D32 depth (also SRV for PostFX)
        RefCntAutoPtr<ITexture> prevSceneDepth; // last frame's finalized sceneDepth
                                                // (SRV only) -- a REAL history buffer,
                                                // snapshotted at the end of EndScene, so
                                                // PostFXContext's depth-based disocclusion
                                                // actually has something to compare
                                                // against instead of the current frame
                                                // reused as its own "previous".
        RefCntAutoPtr<IPipelineState> tonemapPSO;
        RefCntAutoPtr<IShaderResourceBinding> tonemapSRB;
        RefCntAutoPtr<IBuffer> postConstants;
        bool outputSRGB = false; // back buffer is a non-sRGB UNORM
        // 2D editor mode (roadmap #14): mirrors Camera::orthographic, set each SetCamera call.
        // DrawGrid reads it to pick its plane flags -- DrawGrid takes no Camera parameter of
        // its own, so this is how it learns the current mode.
        bool orthographic = false;

        // Debug wireframe overlay (M2.1's collider visualization, DrawWireframe) -- drawn
        // directly onto the resolved back buffer, same "1 RTV, no depth" shape as the
        // tonemap PSO above (see CreateWireframePipeline).
        RefCntAutoPtr<IPipelineState> wireframePSO;
        RefCntAutoPtr<IShaderResourceBinding> wireframeSRB;
        RefCntAutoPtr<IBuffer> wireframeConstants;
        RefCntAutoPtr<IBuffer> wireframeVB; // dynamic, kMaxWireframeVertices capacity

        // Editor backdrop (roadmap #12): a gradient sky (fullscreen pass, drawn into the HDR
        // G-buffer before scene geometry) + an infinite ground grid (DiligentFX's
        // CoordinateGridRenderer, drawn onto the resolved back buffer after EndScene -- see
        // CreateSkyPipeline/CreateGridRenderer and DrawSky/DrawGrid).
        RefCntAutoPtr<IPipelineState> skyPSO;
        RefCntAutoPtr<IShaderResourceBinding> skySRB;
        RefCntAutoPtr<IBuffer> skyConstants;
        std::unique_ptr<CoordinateGridRenderer> gridRenderer;
        HLSL::CoordinateGridAttribs gridAttribs{};

        // 2D sprites (roadmap #13): a transparent textured-quad pass drawn into the HDR
        // G-buffer after opaque geometry, before EndScene (see CreateSpritePipeline/
        // DrawSprite). g_SpriteTex is DYNAMIC on one shared SRB, re-Set per draw -- the same
        // "one texture varies every call" shape as the model fill PSOs' g_Albedo/modelSRB
        // above, not a per-texture SRB cache. spriteQuadVB is a single IMMUTABLE unit quad
        // (pos+uv, 6 verts, -0.5..0.5 XY) shared by every sprite draw; only the world
        // matrix/tint/UV-rect constants differ per sprite.
        RefCntAutoPtr<IPipelineState> spritePSO;
        RefCntAutoPtr<IShaderResourceBinding> spriteSRB;
        RefCntAutoPtr<IBuffer> spriteConstants;
        RefCntAutoPtr<IBuffer> spriteQuadVB;

        // In-game UI overlay (roadmap #17): screen-space tinted quads + MSDF text drawn onto the
        // resolved back buffer after EndScene (see CreateUIPipeline/DrawUI). One dynamic VB is
        // remapped per batch; g_UIAtlas is DYNAMIC on the shared SRB, re-Set per DrawUI call (the
        // font atlas, or uiWhiteTex for a solid-only batch) -- the same per-draw-varying-texture
        // shape as spriteSRB's g_SpriteTex above.
        RefCntAutoPtr<IPipelineState> uiPSO;
        RefCntAutoPtr<IShaderResourceBinding> uiSRB;
        RefCntAutoPtr<IBuffer> uiConstants;
        RefCntAutoPtr<IBuffer> uiVB;
        RefCntAutoPtr<ITexture> uiWhiteTex; // 1x1 white, bound when DrawUI gets no atlas (solid quads)

        // DiligentFX post effects (Bloom + SSAO) share a PostFXContext. It requires
        // depth + motion + camera to reach its "PSOs ready" gate (which both effects
        // check). Bloom ignores those inputs; SSAO/TAA/SSR read real depth + a
        // world-space normal G-buffer + camera, and reproject using both. So when a
        // post effect runs we fill `postCamera` from the actual view/proj (not zeros)
        // and the scene writes `normalBuffer` + real per-pixel `motionVectors`.
        std::unique_ptr<PostFXContext> postFX;
        std::unique_ptr<Bloom> bloom;
        std::unique_ptr<ScreenSpaceAmbientOcclusion> ssao;
        std::unique_ptr<DepthOfField> dof;
        std::unique_ptr<TemporalAntiAliasing> taa;
        std::unique_ptr<ScreenSpaceReflection> ssr;
        float2 frameJitter{0.0f, 0.0f};        // sub-pixel proj jitter this frame (TAA)
        RefCntAutoPtr<ITexture> motionVectors; // RG16F NDC velocity (scene-written)
        RefCntAutoPtr<ITexture> aoWhite;       // 1x1 white = "fully visible" default
        RefCntAutoPtr<ITexture> ssrBlack;      // 1x1 black = "no reflection" default
        RefCntAutoPtr<ITexture> modelWhite;    // 1x1 white 2D-ARRAY = untextured model albedo

        // Cascaded shadow maps (Diligent's ShadowMapManager) -- a forward-rendering
        // technique, not a PostFXContext effect: rendered before BeginFrame, into its own
        // depth-only cascade targets, then sampled by the main fill PSOs (toon_fill.hlsl /
        // model_fill.hlsl's ComputeShadowFactor). See Renderer::BeginShadowPass.
        ShadowMapManager shadowMap;
        // Bare (not HLSL::-nested): ShadowMapManager.cpp is a separate translation unit
        // that always resolves this to plain Diligent::ShadowMapAttribs -- see the #undef
        // block near the top of this file for why that's a hard requirement, not a style
        // choice.
        ShadowMapAttribs shadowMapAttribs{}; // filled by DistributeCascades each frame
        RefCntAutoPtr<IBuffer> shadowAttribsCB;    // uploaded once/frame; read by the fill PSOs
        RefCntAutoPtr<IBuffer> shadowDrawConstants; // per-draw light-space WVP (shadow pass only)
        RefCntAutoPtr<IPipelineState> shadowPSO;         // depth-only, procedural mesh layout
        RefCntAutoPtr<IShaderResourceBinding> shadowSRB;
        RefCntAutoPtr<IPipelineState> modelShadowPSO; // depth-only, glTF model layout
        RefCntAutoPtr<IShaderResourceBinding> modelShadowSRB;
        // Roadmap #11 (skeletal animation): depth-only, animated glTF model layout (adds
        // JOINTS_0/WEIGHTS_0, buffer 1 -- see ModelVertexAttribs); skins position so a
        // moving character's shadow follows its animated silhouette. See Impl::modelSkinnedPSO
        // above for why this is a separate PSO rather than a branch in modelShadowPSO's shader.
        RefCntAutoPtr<IPipelineState> modelShadowSkinnedPSO;
        RefCntAutoPtr<IShaderResourceBinding> modelShadowSkinnedSRB;
        RefCntAutoPtr<ISampler> shadowSampler; // comparison sampler for g_ShadowMap_sampler
        Uint32 currentShadowCascade = 0;       // set by BeginShadowCascade, read by DrawMeshShadow/DrawModelShadow

        HLSL::CameraAttribs postCamera{};
        // Last frame's postCamera -- a REAL previous-camera snapshot for PostFXContext
        // (see RunPostFX). Without this, PostFXContext's own camera-matrix-based
        // reprojection (ComputeReprojectedDepth.fx, which SSAO/TAA/SSR's disocclusion
        // checks all read via GetReprojectedDepth()) can't distinguish "the camera
        // moved" from "the camera didn't move" -- it always sees whatever `postCamera`
        // currently holds as BOTH the current and previous camera. Every temporal
        // effect's history-trust test then silently assumes the camera never orbits,
        // pans, zooms, or flies, which is wrong every frame the camera actually moves.
        HLSL::CameraAttribs prevPostCamera{};
        bool havePrevPostCamera = false; // seed prev = curr on the very first frame
        Uint32 frameIndex = 0;

        // Run the shared PostFXContext plus whichever effects are enabled. The color
        // effects chain in order scene -> TAA -> DoF -> Bloom; `colorOut` is the last
        // stage's output (or null -> resolve the raw scene). `aoOut` (SSAO visibility) and
        // `ssrOut` (reflection radiance) composite in the resolve; null -> their defaults.
        // No-op if no effect is enabled.
        void RunPostFX(const PostParams &p, ITextureView *&colorOut, ITextureView *&aoOut, ITextureView *&ssrOut);

        // Fill postCamera from the stored view/proj so PostFXContext/SSAO can rebuild
        // view-space positions from depth. (Bloom doesn't use it; SSAO does.)
        void FillCameraAttribs(const SwapChainDesc &sc);

        // Per-frame scene state. view/proj are kept split (not just viewProj) because
        // the camera attribs above need each one and their inverses. prevViewProj is last
        // frame's, so DrawMesh can build each object's previous clip position for motion.
        float4x4 view = float4x4::Identity();
        float4x4 proj = float4x4::Identity();
        float4x4 viewProj = float4x4::Identity();
        float4x4 prevViewProj = float4x4::Identity();
        float nearZ = 0.1f;
        float farZ = 100.0f;
        float3 lightDir = float3(0.5f, 0.8f, -0.3f);  // world-space dir TO light (shader normalizes)
        float3 lightColor = float3(1.0f, 1.0f, 1.0f); // color * intensity, premultiplied
        PostParams post;
    };

    void Renderer::Impl::FillCameraAttribs(const SwapChainDesc &sc) {
        const float4x4 viewInv = view.Inverse();
        const float W = static_cast<float>(sc.Width);
        const float H = static_cast<float>(sc.Height);

        postCamera.f4ViewportSize = float4(W, H, W > 0.0f ? 1.0f / W : 0.0f, H > 0.0f ? 1.0f / H : 0.0f);
        postCamera.SetClipPlanes(nearZ, farZ); // near < far -> non-reversed [0,1] depth
        postCamera.fSceneNearZ = postCamera.fNearPlaneZ;
        postCamera.fSceneFarZ = postCamera.fFarPlaneZ;
        postCamera.fSceneNearDepth = postCamera.fNearPlaneDepth;
        postCamera.fSceneFarDepth = postCamera.fFarPlaneDepth;
        postCamera.fHandness = view.Determinant() > 0.0f ? 1.0f : -1.0f;
        postCamera.uiFrameIndex = frameIndex;
        postCamera.mView = view;
        postCamera.mProj = proj;
        postCamera.mViewProj = viewProj;
        postCamera.mViewInv = viewInv;
        postCamera.mProjInv = proj.Inverse();
        postCamera.mViewProjInv = viewProj.Inverse();
        postCamera.f4Position = float4(float3::MakeVector(viewInv[3]), 1.0f); // camera world pos

        // Depth-of-field lens parameters (DoF reads its circle-of-confusion from these).
        // Focal length + sensor size keep their struct defaults (50mm / 36mm).
        postCamera.fFocusDistance = post.dofFocusDist;
        postCamera.fFStop = post.dofFStop;

        // Sub-pixel jitter used to render this frame (0 unless TAA is on). PostFX shaders
        // remove it via f2Jitter when reprojecting.
        postCamera.f2Jitter = frameJitter;
    }

    void Renderer::Impl::RunPostFX(const PostParams &p, ITextureView *&colorOut, ITextureView *&aoOut,
                                   ITextureView *&ssrOut) {
        colorOut = nullptr;
        aoOut = nullptr;
        ssrOut = nullptr;
        if (!p.bloom && !p.ssao && !p.dof && !p.taa && !p.ssr) { return; }

        const SwapChainDesc &sc = swapChain->GetDesc();
        if (sc.Width == 0 || sc.Height == 0) { return; }

        // DoF uses the motion vectors (via the context) to smooth its circle-of-confusion
        // over time, safe now that motion is real.
        const auto dofFlags = DepthOfField::FEATURE_FLAG_ENABLE_TEMPORAL_SMOOTHING;
        const auto taaFlags = TemporalAntiAliasing::FEATURE_FLAG_BICUBIC_FILTER;

        // Prepare the shared context + the enabled effects (each early-outs on unchanged
        // size). The PSOs are what PostFXContext::IsPSOsReady() gates on below.
        PostFXContext::FrameDesc frame;
        frame.Index = frameIndex;
        frame.Width = sc.Width;
        frame.Height = sc.Height;
        postFX->PrepareResources(device, frame, PostFXContext::FEATURE_FLAG_NONE);
        if (p.bloom) { bloom->PrepareResources(device, context, postFX.get(), Bloom::FEATURE_FLAG_NONE); }
        if (p.ssao) {
            ssao->PrepareResources(device, context, postFX.get(), ScreenSpaceAmbientOcclusion::FEATURE_FLAG_NONE);
        }
        if (p.dof) { dof->PrepareResources(device, context, postFX.get(), dofFlags); }
        if (p.taa) { taa->PrepareResources(device, context, postFX.get(), taaFlags); }
        if (p.ssr) { ssr->PrepareResources(device, context, postFX.get(), ScreenSpaceReflection::FEATURE_FLAG_NONE); }

        // Run the shared context: real camera + a real depth history (curr = this frame's
        // sceneDepth, prev = last frame's, snapshotted into prevSceneDepth at the end of
        // EndScene) + the scene's motion vectors. Computes the reprojected-depth /
        // closest-motion / blue-noise resources the effects build on and flips
        // IsPSOsReady() true. The real prev depth is what lets each effect's own
        // depth-based disocclusion fire, instead of leaning on motion vectors alone
        // (see prevSceneDepth's declaration).
        FillCameraAttribs(sc);
        if (!havePrevPostCamera) {
            prevPostCamera = postCamera; // first frame: no real history yet -- zero apparent motion
            havePrevPostCamera = true;
        }
        ITextureView *depthSRV = sceneDepth->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
        ITextureView *prevDepthSRV = prevSceneDepth->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
        ITextureView *motionSRV = motionVectors->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);

        PostFXContext::RenderAttributes pfx;
        pfx.pDevice = device;
        pfx.pDeviceContext = context;
        pfx.pCurrDepthBufferSRV = depthSRV;
        pfx.pPrevDepthBufferSRV = prevDepthSRV;
        pfx.pMotionVectorsSRV = motionSRV;
        pfx.pCurrCamera = &postCamera;
        pfx.pPrevCamera = &prevPostCamera;
        postFX->Execute(pfx);
        ++frameIndex;

        // Snapshot now that Execute has consumed prevPostCamera as "previous" for THIS
        // frame's reprojection -- ready to be genuinely previous for NEXT frame. Mirrors
        // prevSceneDepth's CopyTexture at the end of EndScene (same reasoning, same
        // ordering constraint: update the history only after this frame's read of it).
        prevPostCamera = postCamera;

        if (!postFX->IsPSOsReady()) {
            return; // still compiling: skip effects this frame
        }

        if (p.ssao) {
            HLSL::ScreenSpaceAmbientOcclusionAttribs attribs{};
            attribs.EffectRadius = p.ssaoRadius;
            // 1 = current frame only (no ghosting) -- also force it per
            // PostParams::suppressTemporalHistory's comment.
            attribs.ResetAccumulation = (p.ssaoTemporal && !p.suppressTemporalHistory) ? 0 : 1;

            ScreenSpaceAmbientOcclusion::RenderAttributes ra;
            ra.pDevice = device;
            ra.pDeviceContext = context;
            ra.pPostFXContext = postFX.get();
            ra.pDepthBufferSRV = depthSRV;
            ra.pNormalBufferSRV = normalBuffer->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
            ra.pSSAOAttribs = &attribs;
            ssao->Execute(ra);
            aoOut = ssao->GetAmbientOcclusionSRV();
        }

        if (p.ssr) {
            // Roughness rides in the normal buffer's .w, so it's both the normal and the
            // material input (RoughnessChannel = 3 selects .w).
            ITextureView *normalSRV = normalBuffer->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);

            HLSL::ScreenSpaceReflectionAttribs attribs{};
            attribs.RoughnessChannel = 3;      // roughness is in normal.w
            attribs.IsRoughnessPerceptual = 1; // we store artist roughness, not squared
            // SSR has no ResetAccumulation escape hatch (unlike SSAO/TAA above), and the library
            // default (1.0) is the most ghosting-prone end of its own documented range ("higher
            // values ... more likely to exhibit ghosting artefacts") -- same root cause (no real
            // prev-depth history). Trade a bit more reflection noise for a lot less lingering
            // ghost, since we can't reset this one for the duration of an interaction.
            attribs.TemporalRadianceStabilityFactor = 0.7f; // library default: 1.0

            ScreenSpaceReflection::RenderAttributes ra;
            ra.pDevice = device;
            ra.pDeviceContext = context;
            ra.pPostFXContext = postFX.get();
            ra.pColorBufferSRV = hdrColor->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
            ra.pDepthBufferSRV = depthSRV;
            ra.pNormalBufferSRV = normalSRV;
            ra.pMaterialBufferSRV = normalSRV; // roughness packed in .w
            ra.pMotionVectorsSRV = motionSRV;
            ra.pSSRAttribs = &attribs;
            ssr->Execute(ra);
            ssrOut = ssr->GetSSRRadianceSRV();
        }

        // Color chain: scene -> TAA (resolve) -> DoF (depth blur) -> Bloom (glow). Each
        // enabled stage reads the previous stage's output, so colorOut ends on the last
        // one that ran. TAA is first so DoF/Bloom process the anti-aliased image.
        ITextureView *colorSRV = hdrColor->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);

        if (p.taa) {
            HLSL::TemporalAntiAliasingAttribs attribs{}; // defaults (stability 0.9375)
            // Same reasoning as SSAO's ResetAccumulation above.
            attribs.ResetAccumulation = p.suppressTemporalHistory;

            TemporalAntiAliasing::RenderAttributes tra;
            tra.pDevice = device;
            tra.pDeviceContext = context;
            tra.pPostFXContext = postFX.get();
            tra.pColorBufferSRV = colorSRV;
            tra.pTAAAttribs = &attribs;
            taa->Execute(tra);
            colorSRV = taa->GetAccumulatedFrameSRV();
            colorOut = colorSRV;
        }

        if (p.dof) {
            HLSL::DepthOfFieldAttribs attribs{};
            attribs.MaxCircleOfConfusion = p.dofMaxCoC; // focus/aperture live in the camera attribs

            DepthOfField::RenderAttributes dra;
            dra.pDevice = device;
            dra.pDeviceContext = context;
            dra.pPostFXContext = postFX.get();
            dra.pColorBufferSRV = colorSRV;
            dra.pDepthBufferSRV = depthSRV;
            dra.pDOFAttribs = &attribs;
            dof->Execute(dra);
            colorSRV = dof->GetDepthOfFieldTextureSRV();
            colorOut = colorSRV;
        }

        if (p.bloom) {
            HLSL::BloomAttribs attribs{};
            attribs.Intensity = p.bloomIntensity;
            attribs.Threshold = p.bloomThreshold;
            attribs.SoftTreshold = p.bloomSoftKnee; // (sic: DiligentFX's field spelling)
            attribs.Radius = p.bloomRadius;

            Bloom::RenderAttributes bra;
            bra.pDevice = device;
            bra.pDeviceContext = context;
            bra.pPostFXContext = postFX.get();
            bra.pColorBufferSRV = colorSRV;
            bra.pBloomAttribs = &attribs;
            bloom->Execute(bra);
            colorSRV = bloom->GetBloomTextureSRV();
            colorOut = colorSRV;
        }
    }

    // --- File-local helpers -----------------------------------------------------

    // Fill Diligent's NativeWindow from a GLFW window, per platform.
    static NativeWindow MakeNativeWindow(GLFWwindow *wnd) {
        NativeWindow nw{};
#if defined(_WIN32)
        nw.hWnd = glfwGetWin32Window(wnd);
#elif defined(__linux__)
        nw.WindowId = static_cast<Uint32>(glfwGetX11Window(wnd));
        nw.pDisplay = glfwGetX11Display();
#elif defined(__APPLE__)
        // macOS needs the NSView of the Cocoa window (a few lines of Objective-C++;
        // lift it from Diligent's GLFWDemo Cocoa helper) before this will build.
#error "Set nw.pNSView from GLFWDemo's Cocoa helper before building on macOS."
#endif
        return nw;
    }

    // Compile one HLSL shader stage from the shaders directory. `compileFlags` defaults to
    // none; toon_fill.hlsl/model_fill.hlsl's shaders pass PACK_MATRIX_ROW_MAJOR (see
    // Renderer::CreateToonPipeline) because DiligentFX's ShadowMapAttribs/CascadeAttribs
    // (Shaders/Common/public/BasicStructures.fxh) declare their float4x4 fields with no
    // explicit row_major/column_major keyword -- unlike our own Constants cbuffer, which
    // marks every matrix row_major explicitly and so is unaffected by this flag either way.
    // `cache` instead of a raw IRenderDevice*: every shader in this file is created through
    // Renderer::Impl::stateCache (roadmap #10, shader hot-reload) instead of the device
    // directly, in every build -- see stateCache's own comment (Impl, above).
    static RefCntAutoPtr<IShader> CreateToonShader(IRenderStateCache *cache, IShaderSourceInputStreamFactory *factory,
                                                   SHADER_TYPE type, const char *name, const char *file,
                                                   const char *entry,
                                                   SHADER_COMPILE_FLAGS compileFlags = SHADER_COMPILE_FLAG_NONE) {
        ShaderCreateInfo ci;
        ci.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
        ci.Desc.ShaderType = type;
        ci.Desc.Name = name;
        ci.Desc.UseCombinedTextureSamplers = true;
        ci.EntryPoint = entry;
        ci.FilePath = file;
        ci.pShaderSourceStreamFactory = factory;
        ci.CompileFlags = compileFlags;

        RefCntAutoPtr<IShader> shader;
        cache->CreateShader(ci, &shader);
        return shader;
    }

    // Vertex attributes we ask the glTF loader to produce, for EVERY model load, skinned or
    // not: POSITION/NORMAL/TEXCOORD_0 interleaved into buffer 0 (unchanged since before
    // roadmap #11 -- this is what keeps an unskinned model's existing draw path byte-for-byte
    // identical), plus JOINTS_0/WEIGHTS_0 interleaved into buffer 1 (matching DiligentTools'
    // own DefaultVertexAttributes' choice of buffer slot for these two). A file with no skin
    // at all just leaves buffer 1 unread -- only the skinned PSOs' input layout references it
    // (see CreateSkinnedModelPipeline); the static PSOs' layout still only references buffer
    // 0, so binding buffer 1 alongside it (DrawModel's existing GetVertexBufferCount loop
    // already binds however many buffers exist, unchanged) is simply ignored.
    //
    // A primitive with no JOINTS_0/WEIGHTS_0 of its own (any primitive in an unskinned model,
    // or a stray unskinned primitive inside an otherwise-skinned one) gets the DEFAULT weight
    // below instead of the loader's usual zero-fill: (1,0,0,0) always fully follows whatever
    // matrix ends up at the per-draw joint palette's slot 0. All-zero weights would instead
    // collapse the vertex to the origin (a zero matrix), since nothing in the skin blend would
    // sum to identity.
    static const GLTF::VertexAttributeDesc *ModelVertexAttribs(size_t &count) {
        static constexpr float4 kDefaultJointWeight{1.0f, 0.0f, 0.0f, 0.0f};
        static const GLTF::VertexAttributeDesc kAttribs[] = {
            {GLTF::PositionAttributeName, 0, VT_FLOAT32, 3},
            {GLTF::NormalAttributeName, 0, VT_FLOAT32, 3},
            {GLTF::Texcoord0AttributeName, 0, VT_FLOAT32, 2},
            {GLTF::JointsAttributeName, 1, VT_FLOAT32, 4},
            {GLTF::WeightsAttributeName, 1, VT_FLOAT32, 4, &kDefaultJointWeight},
        };
        count = sizeof(kAttribs) / sizeof(kAttribs[0]);
        return kAttribs;
    }

    // Roadmap #11 (skeletal animation): grows m_impl->jointsBuffer (never shrinks it) to
    // hold at least `neededElements` float4x4 matrices, re-pointing every skinned SRB's
    // g_Joints variable at the new buffer's SRV when it actually grows (rare -- only the
    // first draw of a session's largest skin, or a later one that beats it). Called once
    // per skinned draw, before that draw's own MAP_WRITE+MAP_FLAG_DISCARD upload. A member
    // function, not a free one like CreateToonShader above: it needs m_impl access, and
    // Impl is private to Renderer.
    void Renderer::EnsureJointsBufferCapacity(uint32_t neededElements) {
        if (neededElements <= m_impl->jointsBufferCapacity) { return; }

        BufferDesc bd;
        bd.Name = "skinning joints buffer";
        bd.Usage = USAGE_DYNAMIC;
        bd.BindFlags = BIND_SHADER_RESOURCE;
        bd.CPUAccessFlags = CPU_ACCESS_WRITE;
        bd.Mode = BUFFER_MODE_STRUCTURED;
        bd.ElementByteStride = sizeof(float4x4);
        bd.Size = Uint64{neededElements} * sizeof(float4x4);
        m_impl->jointsBuffer.Release();
        m_impl->device->CreateBuffer(bd, nullptr, &m_impl->jointsBuffer);
        m_impl->jointsBufferCapacity = m_impl->jointsBuffer ? neededElements : 0;
        if (!m_impl->jointsBuffer) { return; }

        IBufferView *jointsSRV = m_impl->jointsBuffer->GetDefaultView(BUFFER_VIEW_SHADER_RESOURCE);
        IShaderResourceBinding *skinnedSRBs[] = {m_impl->modelSkinnedSRB, m_impl->modelOutlineSkinnedSRB,
                                                 m_impl->modelShadowSkinnedSRB};
        for (IShaderResourceBinding *srb : skinnedSRBs) {
            // Every skinned SRB already exists by the time any draw can call this (they're
            // created during Init, before the render loop starts) -- null-checked anyway,
            // matching this file's usual "set if present" defensiveness elsewhere.
            if (!srb) { continue; }
            if (auto *v = srb->GetVariableByName(SHADER_TYPE_VERTEX, "g_Joints")) { v->Set(jointsSRV); }
        }
    }

    // --- Construction & device / swap-chain bring-up ----------------------------

    Renderer::Renderer() : m_impl(new Impl) {}
    Renderer::~Renderer() {
        Shutdown();
        delete m_impl;
        m_impl = nullptr;
    }

    bool Renderer::Init(GLFWwindow *window) {
#if ENGINE_DLL
        // Shared-library build: load the Vulkan engine DLL and fetch its factory.
        auto GetEngineFactoryVk = LoadGraphicsEngineVk();
#endif
        IEngineFactoryVk *factory = GetEngineFactoryVk();

        EngineVkCreateInfo engineCI;
        // Requested, not left to default (DEVICE_FEATURE_STATE_DISABLED): CreateSpritePipeline
        // (roadmap #13) sets BlendDesc.IndependentBlendEnable = True unconditionally, with no
        // fallback if the underlying Vulkan device feature isn't actually enabled -- without
        // this, vkCreateGraphicsPipelines throws a real validation error (VUID-VkPipeline-
        // ColorBlendStateCreateInfo-pAttachments-00605) every launch, since the sprite PSO's
        // RT0 (alpha-blended color) and RT1/RT2 (write-masked off) are genuinely different
        // per-target blend states. Diligent's own D3D and WebGPU factories already default
        // this to ENABLED in their own engine creation; Vulkan leaves it to the app to request.
        // independentBlend is core Vulkan 1.0 and near-universally supported, so ENABLED
        // (hard-required) rather than OPTIONAL matches what the sprite PSO already assumes,
        // instead of silently degrading back into the bug this fixes on exotic hardware.
        engineCI.Features.IndependentBlend = DEVICE_FEATURE_STATE_ENABLED;
        factory->CreateDeviceAndContextsVk(engineCI, &m_impl->device, &m_impl->context);
        if (!m_impl->device) {
            std::fprintf(stderr, "Renderer: failed to create Vulkan render device\n");
            return false;
        }

        SwapChainDesc scDesc;
        NativeWindow window_ = MakeNativeWindow(window);
        factory->CreateSwapChainVk(m_impl->device, m_impl->context, scDesc, window_, &m_impl->swapChain);
        if (!m_impl->swapChain) {
            std::fprintf(stderr, "Renderer: failed to create swap chain\n");
            return false;
        }

        // Shader source loader (reads the .hlsl files, resolves #include). Wrapped in a
        // compound factory so our own shaders can ALSO #include DiligentFX's shared headers
        // (Shadows.fxh, BasicStructures.fxh) -- those are embedded in the DiligentFX lib, not
        // under assets/shaders, so our own factory alone can't resolve them. The compound
        // factory tries ours first (existing #include "toon_common.hlsli" etc. still resolve
        // exactly as before), falling back to DiligentFX's for names only it has.
        const std::string shadersDir = Assets::Shaders(); // exe-relative (or dev-fallback) HLSL source dir
        factory->CreateDefaultShaderSourceStreamFactory(shadersDir.c_str(), &m_impl->shaderFactory);
        if (!m_impl->shaderFactory) {
            std::fprintf(stderr, "Renderer: failed to create shader source factory for '%s'\n", shadersDir.c_str());
            return false;
        }
        m_impl->shaderFactory = CreateCompoundShaderSourceFactory(
            {m_impl->shaderFactory, &DiligentFXShaderSourceStreamFactory::GetInstance()});

        // Roadmap #10 (shader hot-reload): every shader/PSO below is created through this
        // cache, in every build (see Impl::stateCache's own comment) -- only EnableHotReload
        // and the watcher differ between Debug and Release.
        {
            IArchiverFactory *archiverFactory = LoadAndGetArchiverFactory();
            if (!archiverFactory) {
                std::fprintf(stderr, "Renderer: failed to load the Archiver factory\n");
                return false;
            }
            RenderStateCacheCreateInfo cacheCI;
            cacheCI.pDevice = m_impl->device;
            cacheCI.pArchiverFactory = archiverFactory;
            cacheCI.FileHashMode = RENDER_STATE_CACHE_FILE_HASH_MODE_BY_CONTENT;
#ifdef TOON_SHADER_HOT_RELOAD
            cacheCI.EnableHotReload = true;
#endif
            CreateRenderStateCache(cacheCI, &m_impl->stateCache);
            if (!m_impl->stateCache) {
                std::fprintf(stderr, "Renderer: failed to create the shader/PSO state cache\n");
                return false;
            }
        }
#ifdef TOON_SHADER_HOT_RELOAD
        // Watches assets/shaders/ for a saved .hlsl/.hlsli file (ShaderReloadListener above);
        // BeginFrame checks shadersDirty once per frame and calls stateCache->Reload() when
        // it's set. Non-recursive: every .hlsl lives flat in this one directory.
        m_impl->shaderListener = std::make_unique<ShaderReloadListener>(m_impl->shadersDirty);
        m_impl->shaderWatcher.addWatch(Assets::Shaders(), m_impl->shaderListener.get(), /*recursive=*/false);
        m_impl->shaderWatcher.watch();
#endif

        // Before the toon/model pipelines: CreateShadowMap builds the shadow map atlas +
        // its own depth-only PSOs, and CreateToonPipeline/CreateModelPipeline bind it into
        // the fill PSOs as a STATIC variable, which must happen before those PSOs' SRBs are
        // created (a static variable can't be set after CreateShaderResourceBinding).
        if (!CreateShadowMap()) {
            std::fprintf(stderr, "Renderer: failed to create shadow map\n");
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

        if (!CreateSkinnedModelPipeline()) {
            std::fprintf(stderr, "Renderer: failed to create skinned model pipeline\n");
            return false;
        }

        // HDR pipeline: offscreen scene target + tone-map resolve to the back buffer.
        const SwapChainDesc &sc = m_impl->swapChain->GetDesc();
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

        if (!CreateWireframePipeline()) {
            std::fprintf(stderr, "Renderer: failed to create wireframe pipeline\n");
            return false;
        }

        if (!CreateSkyPipeline()) {
            std::fprintf(stderr, "Renderer: failed to create sky pipeline\n");
            return false;
        }

        if (!CreateGridRenderer()) {
            std::fprintf(stderr, "Renderer: failed to create grid renderer\n");
            return false;
        }

        if (!CreateSpritePipeline()) {
            std::fprintf(stderr, "Renderer: failed to create sprite pipeline\n");
            return false;
        }

        if (!CreateUIPipeline()) {
            std::fprintf(stderr, "Renderer: failed to create UI pipeline\n");
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
        m_impl->prevSceneDepth.Release();
        m_impl->motionVectors.Release();

        TextureDesc cd;
        cd.Name = "HDR scene color";
        cd.Type = RESOURCE_DIM_TEX_2D;
        cd.Width = width;
        cd.Height = height;
        cd.MipLevels = 1;
        cd.Format = kHDRFormat;
        cd.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;
        m_impl->device->CreateTexture(cd, nullptr, &m_impl->hdrColor);

        // World-space normal G-buffer (second scene render target), read by SSAO.
        TextureDesc nd;
        nd.Name = "scene normals";
        nd.Type = RESOURCE_DIM_TEX_2D;
        nd.Width = width;
        nd.Height = height;
        nd.MipLevels = 1;
        nd.Format = kNormalFormat;
        nd.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;
        m_impl->device->CreateTexture(nd, nullptr, &m_impl->normalBuffer);

        // Depth doubles as a shader resource: PostFXContext reads it as an SRV (the
        // Vulkan backend exposes D32 depth as R32_FLOAT). BIND_SHADER_RESOURCE is the
        // only difference from a plain depth target.
        TextureDesc dd;
        dd.Name = "scene depth";
        dd.Type = RESOURCE_DIM_TEX_2D;
        dd.Width = width;
        dd.Height = height;
        dd.MipLevels = 1;
        dd.Format = kSceneDepthFormat;
        dd.BindFlags = BIND_DEPTH_STENCIL | BIND_SHADER_RESOURCE;
        m_impl->device->CreateTexture(dd, nullptr, &m_impl->sceneDepth);

        // A real previous-frame depth history for PostFXContext. Needs BIND_DEPTH_STENCIL
        // even though it's never bound as a DSV -- dropping it trips Vulkan validation
        // errors on the SRV's depth->R32_FLOAT reinterpretation (VUID-VkImageViewCreateInfo-
        // image-01762 / -subresourceRange-09594), so its creation flags must match
        // sceneDepth's. EndScene copies sceneDepth into it once per frame; undefined for
        // one frame on startup (same as prevViewProj starting as identity).
        TextureDesc pd = dd;
        pd.Name = "prev scene depth";
        m_impl->device->CreateTexture(pd, nullptr, &m_impl->prevSceneDepth);

        // Screen-space motion-vector target (third scene render target): NDC velocity per
        // pixel, read by SSAO temporal accumulation (and DoF). Cleared + written each
        // frame by the toon passes; PostFXContext then reads it.
        TextureDesc md;
        md.Name = "motion vectors";
        md.Type = RESOURCE_DIM_TEX_2D;
        md.Width = width;
        md.Height = height;
        md.MipLevels = 1;
        md.Format = kMotionFormat;
        md.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;
        m_impl->device->CreateTexture(md, nullptr, &m_impl->motionVectors);

        return m_impl->hdrColor && m_impl->normalBuffer && m_impl->sceneDepth && m_impl->prevSceneDepth &&
               m_impl->motionVectors;
    }

    // Cascaded shadow maps: Diligent's ShadowMapManager owns cascade distribution + the
    // shadow-map atlas (an array texture, one slice per cascade). Depth-only PSOs render
    // the scene into it from the light's viewpoint (Renderer::DrawMeshShadow/DrawModelShadow);
    // the main fill PSOs then sample it via Shadows.fxh's FilterShadowMap (see
    // toon_common.hlsli's ComputeShadowFactor). Must run before CreateToonPipeline /
    // CreateModelPipeline (see Init) so those can bind g_ShadowMap as a STATIC variable
    // before their SRBs are created -- a static variable can't be set afterward.
    bool Renderer::CreateShadowMap() {
        IRenderDevice *device = m_impl->device;
        IRenderStateCache *cache = m_impl->stateCache; // roadmap #10: shaders/PSOs route through this, not `device`

        ShadowMapManager::InitInfo smInfo;
        smInfo.Format = kSceneDepthFormat;
        smInfo.Resolution = kShadowResolution;
        smInfo.NumCascades = kShadowCascades;
        smInfo.ShadowMode = SHADOW_MODE_PCF;
        m_impl->shadowMap.Initialize(device, nullptr, smInfo);

        // Comparison sampler for g_ShadowMap_sampler's hardware PCF (SampleCmp): LESS ("is
        // this pixel's depth less than the stored occluder depth" -> lit). BORDER address
        // with a white border so sampling past every cascade's edge reads as fully lit, not
        // an arbitrary wrapped/clamped shadow-map texel.
        SamplerDesc shadowSamplerDesc;
        shadowSamplerDesc.MinFilter = FILTER_TYPE_COMPARISON_LINEAR;
        shadowSamplerDesc.MagFilter = FILTER_TYPE_COMPARISON_LINEAR;
        shadowSamplerDesc.MipFilter = FILTER_TYPE_COMPARISON_LINEAR;
        shadowSamplerDesc.ComparisonFunc = COMPARISON_FUNC_LESS;
        shadowSamplerDesc.AddressU = TEXTURE_ADDRESS_BORDER;
        shadowSamplerDesc.AddressV = TEXTURE_ADDRESS_BORDER;
        shadowSamplerDesc.AddressW = TEXTURE_ADDRESS_BORDER;
        shadowSamplerDesc.BorderColor[0] = shadowSamplerDesc.BorderColor[1] = shadowSamplerDesc.BorderColor[2] =
            shadowSamplerDesc.BorderColor[3] = 1.0f;
        device->CreateSampler(shadowSamplerDesc, &m_impl->shadowSampler);
        if (!m_impl->shadowSampler) { return false; }

        // Combined-sampler mode (UseCombinedTextureSamplers = true, set on every shader in
        // this file) attaches a texture's sampler to the TEXTURE VIEW itself, not as a
        // separately bindable "g_ShadowMap_sampler" SRB/PSO variable -- unlike g_Albedo/
        // g_HDRColor's ImmutableSamplerDesc, ITextureView::SetSampler is the mechanism here
        // (confirmed by a real Vulkan validation error when this was missing: "no sampler is
        // set in texture view 'Default SRV of texture 'Shadow map SRV''"). The view keeps its
        // own strong ref, so this is the one place the sampler needs to be attached.
        m_impl->shadowMap.GetSRV()->SetSampler(m_impl->shadowSampler);

        // ShadowMapAttribs constant buffer -- filled once per frame (BeginShadowPass) by
        // DistributeCascades, read by the main fill PSOs (ComputeShadowFactor).
        {
            BufferDesc cbDesc;
            cbDesc.Name = "shadow attribs";
            cbDesc.Size = sizeof(ShadowMapAttribs);
            cbDesc.Usage = USAGE_DYNAMIC;
            cbDesc.BindFlags = BIND_UNIFORM_BUFFER;
            cbDesc.CPUAccessFlags = CPU_ACCESS_WRITE;
            device->CreateBuffer(cbDesc, nullptr, &m_impl->shadowAttribsCB);
            if (!m_impl->shadowAttribsCB) { return false; }
        }

        // Per-draw light-space world-view-proj, remapped before each shadow-pass draw --
        // the same MapHelper-per-draw idiom the main toon Constants CB already uses.
        {
            BufferDesc cbDesc;
            cbDesc.Name = "shadow draw constants";
            cbDesc.Size = sizeof(float4x4);
            cbDesc.Usage = USAGE_DYNAMIC;
            cbDesc.BindFlags = BIND_UNIFORM_BUFFER;
            cbDesc.CPUAccessFlags = CPU_ACCESS_WRITE;
            device->CreateBuffer(cbDesc, nullptr, &m_impl->shadowDrawConstants);
            if (!m_impl->shadowDrawConstants) { return false; }
        }

        IShaderSourceInputStreamFactory *sf = m_impl->shaderFactory;

        // Procedural-mesh shadow PSO: same vertex layout as the toon pipeline (position only
        // read; Normal/SmoothNormal are declared just to get the stride right -- see
        // shadow_depth.hlsl). Cull FRONT (render only back faces), the standard shadow-acne
        // mitigation, matching the toon outline PSO's own cull/winding for these primitives.
        // Depth-only: no render targets, no pixel shader.
        {
            auto vs = CreateToonShader(cache, sf, SHADER_TYPE_VERTEX, "shadow VS", "shadow_depth.hlsl", "VSMain");
            if (!vs) { return false; }

            LayoutElement layoutElems[] = {
                LayoutElement{0, 0, 3, VT_FLOAT32, False},
                LayoutElement{1, 0, 3, VT_FLOAT32, False},
                LayoutElement{2, 0, 3, VT_FLOAT32, False},
            };

            GraphicsPipelineStateCreateInfo ci;
            ci.PSODesc.Name = "shadow PSO";
            ci.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;

            GraphicsPipelineDesc &gp = ci.GraphicsPipeline;
            gp.NumRenderTargets = 0;
            gp.DSVFormat = kSceneDepthFormat;
            gp.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            gp.RasterizerDesc.CullMode = CULL_MODE_FRONT;
            gp.RasterizerDesc.FrontCounterClockwise = True; // matches our own primitives' winding
            gp.DepthStencilDesc.DepthEnable = True;
            gp.DepthStencilDesc.DepthWriteEnable = True;
            gp.DepthStencilDesc.DepthFunc = COMPARISON_FUNC_LESS_EQUAL;
            gp.InputLayout.LayoutElements = layoutElems;
            gp.InputLayout.NumElements = sizeof(layoutElems) / sizeof(layoutElems[0]);

            ci.pVS = vs;
            ci.pPS = nullptr; // depth-only
            ci.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;

            cache->CreateGraphicsPipelineState(ci, &m_impl->shadowPSO);
            if (!m_impl->shadowPSO) { return false; }
            if (auto *v = m_impl->shadowPSO->GetStaticVariableByName(SHADER_TYPE_VERTEX, "ShadowConstants")) {
                v->Set(m_impl->shadowDrawConstants);
            }
            m_impl->shadowPSO->CreateShaderResourceBinding(&m_impl->shadowSRB, true);
            if (!m_impl->shadowSRB) { return false; }
        }

        // glTF-model shadow PSO: same vertex layout + winding as the model outline PSO.
        {
            auto vs = CreateToonShader(cache, sf, SHADER_TYPE_VERTEX, "model shadow VS", "model_shadow_depth.hlsl",
                                       "VSMain");
            if (!vs) { return false; }

            LayoutElement modelLayout[] = {
                LayoutElement{0, 0, 3, VT_FLOAT32, False},
                LayoutElement{1, 0, 3, VT_FLOAT32, False},
                LayoutElement{2, 0, 2, VT_FLOAT32, False},
            };

            GraphicsPipelineStateCreateInfo ci;
            ci.PSODesc.Name = "model shadow PSO";
            ci.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;

            GraphicsPipelineDesc &gp = ci.GraphicsPipeline;
            gp.NumRenderTargets = 0;
            gp.DSVFormat = kSceneDepthFormat;
            gp.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            gp.RasterizerDesc.CullMode = CULL_MODE_FRONT;
            gp.RasterizerDesc.FrontCounterClockwise = False; // model winding (matches its outline PSO)
            gp.DepthStencilDesc.DepthEnable = True;
            gp.DepthStencilDesc.DepthWriteEnable = True;
            gp.DepthStencilDesc.DepthFunc = COMPARISON_FUNC_LESS_EQUAL;
            gp.InputLayout.LayoutElements = modelLayout;
            gp.InputLayout.NumElements = sizeof(modelLayout) / sizeof(modelLayout[0]);

            ci.pVS = vs;
            ci.pPS = nullptr;
            ci.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;

            cache->CreateGraphicsPipelineState(ci, &m_impl->modelShadowPSO);
            if (!m_impl->modelShadowPSO) { return false; }
            if (auto *v = m_impl->modelShadowPSO->GetStaticVariableByName(SHADER_TYPE_VERTEX, "ShadowConstants")) {
                v->Set(m_impl->shadowDrawConstants);
            }
            m_impl->modelShadowPSO->CreateShaderResourceBinding(&m_impl->modelShadowSRB, true);
            if (!m_impl->modelShadowSRB) { return false; }
        }

        // Roadmap #11 (skeletal animation): animated-model shadow PSO. Same idea as the
        // static one above, but reads JOINTS_0/WEIGHTS_0 from buffer 1 (see
        // ModelVertexAttribs) and skins position before the light-space transform
        // (model_shadow_depth_skinned.hlsl) -- so an animated character's shadow follows its
        // animated silhouette instead of a static bind-pose one. No motion vectors in a
        // depth-only pass, so unlike the fill/outline skinned PSOs this one has no
        // SkinConstants CB: its shader always reads g_Joints from offset 0.
        {
            // PACK_MATRIX_ROW_MAJOR: joints_common.hlsli's g_Joints needs it (see that file's
            // own comment); this shader has no other row-major-ambiguous declaration, but the
            // flag is still required for g_Joints alone.
            auto vs = CreateToonShader(cache, sf, SHADER_TYPE_VERTEX, "model shadow skinned VS",
                                       "model_shadow_depth_skinned.hlsl", "VSMain",
                                       SHADER_COMPILE_FLAG_PACK_MATRIX_ROW_MAJOR);
            if (!vs) { return false; }

            LayoutElement skinnedModelLayout[] = {
                LayoutElement{0, 0, 3, VT_FLOAT32, False}, // ATTRIB0 position (buffer 0)
                LayoutElement{1, 0, 3, VT_FLOAT32, False}, // ATTRIB1 normal (buffer 0)
                LayoutElement{2, 0, 2, VT_FLOAT32, False}, // ATTRIB2 uv (buffer 0)
                LayoutElement{3, 1, 4, VT_FLOAT32, False}, // ATTRIB3 joints (buffer 1)
                LayoutElement{4, 1, 4, VT_FLOAT32, False}, // ATTRIB4 weights (buffer 1)
            };

            GraphicsPipelineStateCreateInfo ci;
            ci.PSODesc.Name = "model shadow skinned PSO";
            ci.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;

            GraphicsPipelineDesc &gp = ci.GraphicsPipeline;
            gp.NumRenderTargets = 0;
            gp.DSVFormat = kSceneDepthFormat;
            gp.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            gp.RasterizerDesc.CullMode = CULL_MODE_FRONT;
            gp.RasterizerDesc.FrontCounterClockwise = False; // model winding (matches its outline PSO)
            gp.DepthStencilDesc.DepthEnable = True;
            gp.DepthStencilDesc.DepthWriteEnable = True;
            gp.DepthStencilDesc.DepthFunc = COMPARISON_FUNC_LESS_EQUAL;
            gp.InputLayout.LayoutElements = skinnedModelLayout;
            gp.InputLayout.NumElements = sizeof(skinnedModelLayout) / sizeof(skinnedModelLayout[0]);

            ci.pVS = vs;
            ci.pPS = nullptr;
            ci.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;
            // g_Joints must be DYNAMIC, not (the default) STATIC: its underlying buffer
            // doesn't exist yet at PSO-creation time (EnsureJointsBufferCapacity creates it
            // lazily, on the first skinned draw) and can be recreated later on growth, and a
            // STATIC variable can only ever be set BEFORE CreateShaderResourceBinding (see
            // this function's own comment above on ShadowConstants). Same "changes over the
            // renderer's lifetime" reasoning as g_HDRColor/g_AO elsewhere in this file.
            ShaderResourceVariableDesc skinnedShadowVars[] = {
                {SHADER_TYPE_VERTEX, "g_Joints", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
            };
            ci.PSODesc.ResourceLayout.Variables = skinnedShadowVars;
            ci.PSODesc.ResourceLayout.NumVariables = sizeof(skinnedShadowVars) / sizeof(skinnedShadowVars[0]);

            cache->CreateGraphicsPipelineState(ci, &m_impl->modelShadowSkinnedPSO);
            if (!m_impl->modelShadowSkinnedPSO) { return false; }
            if (auto *v =
                    m_impl->modelShadowSkinnedPSO->GetStaticVariableByName(SHADER_TYPE_VERTEX, "ShadowConstants")) {
                v->Set(m_impl->shadowDrawConstants);
            }
            m_impl->modelShadowSkinnedPSO->CreateShaderResourceBinding(&m_impl->modelShadowSkinnedSRB, true);
            if (!m_impl->modelShadowSkinnedSRB) { return false; }
        }

        // A touch-up over the struct defaults DistributeCascades doesn't set: a small nonzero
        // world-space PCF filter so the shadow edge isn't a hard single-tap-aliased line (the
        // library default, fFilterWorldSize = 0, means no kernel spread at all). Persists
        // across frames -- DistributeCascades only ever writes cascade geometry.
        m_impl->shadowMapAttribs.fFilterWorldSize = 0.02f;

        return m_impl->shadowPSO && m_impl->shadowSRB && m_impl->modelShadowPSO && m_impl->modelShadowSRB &&
               m_impl->modelShadowSkinnedPSO && m_impl->modelShadowSkinnedSRB;
    }

    bool Renderer::CreatePostPipeline() {
        IRenderDevice *device = m_impl->device;
        IRenderStateCache *cache = m_impl->stateCache; // roadmap #10: shaders/PSOs route through this, not `device`
        const SwapChainDesc &sc = m_impl->swapChain->GetDesc();

        {
            BufferDesc cbDesc;
            cbDesc.Name = "post constants";
            cbDesc.Size = sizeof(PostConstants);
            cbDesc.Usage = USAGE_DYNAMIC;
            cbDesc.BindFlags = BIND_UNIFORM_BUFFER;
            cbDesc.CPUAccessFlags = CPU_ACCESS_WRITE;
            device->CreateBuffer(cbDesc, nullptr, &m_impl->postConstants);
            if (!m_impl->postConstants) { return false; }
        }

        IShaderSourceInputStreamFactory *sf = m_impl->shaderFactory;
        auto vs = CreateToonShader(cache, sf, SHADER_TYPE_VERTEX, "tonemap VS", "tonemap.hlsl", "VSMain");
        auto ps = CreateToonShader(cache, sf, SHADER_TYPE_PIXEL, "tonemap PS", "tonemap.hlsl", "PSMain");
        if (!vs || !ps) { return false; }

        GraphicsPipelineStateCreateInfo ci;
        ci.PSODesc.Name = "tonemap PSO";
        ci.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;

        GraphicsPipelineDesc &gp = ci.GraphicsPipeline;
        gp.NumRenderTargets = 1;
        gp.RTVFormats[0] = sc.ColorBufferFormat; // resolve to the back buffer
        gp.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        gp.RasterizerDesc.CullMode = CULL_MODE_NONE;
        gp.DepthStencilDesc.DepthEnable = False;

        ci.pVS = vs;
        ci.pPS = ps;

        // g_HDRColor / g_AO / g_SSR are DYNAMIC: EndScene re-points them every frame at
        // whichever HDR source we resolve (raw scene or post-FX output), the SSAO result
        // (or a white "no occlusion" default), and the SSR result (or a black "no
        // reflection" default); each also changes on resize. A dynamic variable is the
        // type meant for a per-frame-changing binding: Diligent manages a fresh
        // descriptor each commit. (A MUTABLE variable bakes one binding and rejects being
        // overwritten while a prior frame may still be reading it.) PostConstants never
        // changes binding, so it stays static.
        ShaderResourceVariableDesc vars[] = {
            {SHADER_TYPE_PIXEL, "g_HDRColor", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
            {SHADER_TYPE_PIXEL, "g_AO", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
            {SHADER_TYPE_PIXEL, "g_SSR", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
            {SHADER_TYPE_PIXEL, "PostConstants", SHADER_RESOURCE_VARIABLE_TYPE_STATIC},
        };
        ci.PSODesc.ResourceLayout.Variables = vars;
        ci.PSODesc.ResourceLayout.NumVariables = sizeof(vars) / sizeof(vars[0]);

        SamplerDesc linClamp;
        linClamp.MinFilter = FILTER_TYPE_LINEAR;
        linClamp.MagFilter = FILTER_TYPE_LINEAR;
        linClamp.MipFilter = FILTER_TYPE_LINEAR;
        linClamp.AddressU = TEXTURE_ADDRESS_CLAMP;
        linClamp.AddressV = TEXTURE_ADDRESS_CLAMP;
        linClamp.AddressW = TEXTURE_ADDRESS_CLAMP;
        ImmutableSamplerDesc immSamplers[] = {
            {SHADER_TYPE_PIXEL, "g_HDRColor", linClamp},
            {SHADER_TYPE_PIXEL, "g_AO", linClamp},
            {SHADER_TYPE_PIXEL, "g_SSR", linClamp},
        };
        ci.PSODesc.ResourceLayout.ImmutableSamplers = immSamplers;
        ci.PSODesc.ResourceLayout.NumImmutableSamplers = sizeof(immSamplers) / sizeof(immSamplers[0]);

        cache->CreateGraphicsPipelineState(ci, &m_impl->tonemapPSO);
        if (!m_impl->tonemapPSO) { return false; }
        if (auto *v = m_impl->tonemapPSO->GetStaticVariableByName(SHADER_TYPE_PIXEL, "PostConstants")) {
            v->Set(m_impl->postConstants);
        }
        m_impl->tonemapPSO->CreateShaderResourceBinding(&m_impl->tonemapSRB, true);
        return m_impl->tonemapSRB != nullptr;
    }

    bool Renderer::CreateWireframePipeline() {
        IRenderDevice *device = m_impl->device;
        IRenderStateCache *cache = m_impl->stateCache; // roadmap #10: shaders/PSOs route through this, not `device`
        const SwapChainDesc &sc = m_impl->swapChain->GetDesc();

        {
            BufferDesc cbDesc;
            cbDesc.Name = "wireframe constants";
            cbDesc.Size = sizeof(WireframeConstants);
            cbDesc.Usage = USAGE_DYNAMIC;
            cbDesc.BindFlags = BIND_UNIFORM_BUFFER;
            cbDesc.CPUAccessFlags = CPU_ACCESS_WRITE;
            device->CreateBuffer(cbDesc, nullptr, &m_impl->wireframeConstants);
            if (!m_impl->wireframeConstants) { return false; }
        }
        {
            BufferDesc vbDesc;
            vbDesc.Name = "wireframe VB";
            vbDesc.Usage = USAGE_DYNAMIC;
            vbDesc.BindFlags = BIND_VERTEX_BUFFER;
            vbDesc.CPUAccessFlags = CPU_ACCESS_WRITE;
            vbDesc.Size = static_cast<Uint64>(kMaxWireframeVertices) * sizeof(float3);
            device->CreateBuffer(vbDesc, nullptr, &m_impl->wireframeVB);
            if (!m_impl->wireframeVB) { return false; }
        }

        IShaderSourceInputStreamFactory *sf = m_impl->shaderFactory;
        auto vs = CreateToonShader(cache, sf, SHADER_TYPE_VERTEX, "wireframe VS", "wireframe.hlsl", "VSMain");
        auto ps = CreateToonShader(cache, sf, SHADER_TYPE_PIXEL, "wireframe PS", "wireframe.hlsl", "PSMain");
        if (!vs || !ps) { return false; }

        LayoutElement layoutElems[] = {
            LayoutElement{0, 0, 3, VT_FLOAT32, False}, // ATTRIB0 position
        };

        GraphicsPipelineStateCreateInfo ci;
        ci.PSODesc.Name = "wireframe PSO";
        ci.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;

        GraphicsPipelineDesc &gp = ci.GraphicsPipeline;
        gp.NumRenderTargets = 1;
        gp.RTVFormats[0] = sc.ColorBufferFormat; // same "back buffer only" shape as the tonemap PSO
        gp.PrimitiveTopology = PRIMITIVE_TOPOLOGY_LINE_LIST;
        gp.RasterizerDesc.CullMode = CULL_MODE_NONE;
        gp.DepthStencilDesc.DepthEnable = False; // always-on-top debug overlay -- see DrawWireframe
        gp.InputLayout.LayoutElements = layoutElems;
        gp.InputLayout.NumElements = sizeof(layoutElems) / sizeof(layoutElems[0]);

        ci.pVS = vs;
        ci.pPS = ps;
        ci.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;

        cache->CreateGraphicsPipelineState(ci, &m_impl->wireframePSO);
        if (!m_impl->wireframePSO) { return false; }
        if (auto *v = m_impl->wireframePSO->GetStaticVariableByName(SHADER_TYPE_VERTEX, "Constants")) {
            v->Set(m_impl->wireframeConstants);
        }
        // wireframe.hlsl's Constants cbuffer is referenced by both VSMain and PSMain (g_Color
        // is PS-only), so -- same as Constants in the toon/model PSOs above -- each stage
        // compiles its own copy of the variable and needs its own binding.
        if (auto *p = m_impl->wireframePSO->GetStaticVariableByName(SHADER_TYPE_PIXEL, "Constants")) {
            p->Set(m_impl->wireframeConstants);
        }
        m_impl->wireframePSO->CreateShaderResourceBinding(&m_impl->wireframeSRB, true);
        return m_impl->wireframeSRB != nullptr;
    }

    bool Renderer::CreateSkyPipeline() {
        IRenderDevice *device = m_impl->device;
        IRenderStateCache *cache = m_impl->stateCache; // roadmap #10: shaders/PSOs route through this, not `device`

        {
            BufferDesc cbDesc;
            cbDesc.Name = "sky constants";
            cbDesc.Size = sizeof(SkyConstants);
            cbDesc.Usage = USAGE_DYNAMIC;
            cbDesc.BindFlags = BIND_UNIFORM_BUFFER;
            cbDesc.CPUAccessFlags = CPU_ACCESS_WRITE;
            device->CreateBuffer(cbDesc, nullptr, &m_impl->skyConstants);
            if (!m_impl->skyConstants) { return false; }
        }

        IShaderSourceInputStreamFactory *sf = m_impl->shaderFactory;
        auto vs = CreateToonShader(cache, sf, SHADER_TYPE_VERTEX, "sky VS", "sky_gradient.hlsl", "VSMain");
        auto ps = CreateToonShader(cache, sf, SHADER_TYPE_PIXEL, "sky PS", "sky_gradient.hlsl", "PSMain");
        if (!vs || !ps) { return false; }

        GraphicsPipelineStateCreateInfo ci;
        ci.PSODesc.Name = "sky PSO";
        ci.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;

        // Same 3-target HDR G-buffer shape as the toon PSOs (CreateToonPipeline): the sky
        // draws into the offscreen scene BeginFrame already bound, before opaque geometry.
        // Depth off -- it always fully covers the frame and never writes depth, so
        // BeginFrame's far-plane clear survives for every pixel opaque geometry doesn't
        // later draw over.
        GraphicsPipelineDesc &gp = ci.GraphicsPipeline;
        gp.NumRenderTargets = 3;
        gp.RTVFormats[0] = kHDRFormat;
        gp.RTVFormats[1] = kNormalFormat;
        gp.RTVFormats[2] = kMotionFormat;
        gp.DSVFormat = kSceneDepthFormat; // must match BeginFrame's bound DSV even though DepthEnable is off
        gp.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        gp.RasterizerDesc.CullMode = CULL_MODE_NONE;
        gp.DepthStencilDesc.DepthEnable = False;

        ci.pVS = vs;
        ci.pPS = ps;
        ci.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;

        cache->CreateGraphicsPipelineState(ci, &m_impl->skyPSO);
        if (!m_impl->skyPSO) { return false; }
        if (auto *v = m_impl->skyPSO->GetStaticVariableByName(SHADER_TYPE_PIXEL, "SkyConstants")) {
            v->Set(m_impl->skyConstants);
        }
        m_impl->skyPSO->CreateShaderResourceBinding(&m_impl->skySRB, true);
        return m_impl->skySRB != nullptr;
    }

    bool Renderer::CreateGridRenderer() {
        // DiligentFX's own component (Components/CoordinateGridRenderer, same library as
        // ShadowMapManager above) -- per the guiding principle, adopted instead of hand-
        // porting ToonEngineOld's grid.frag (see MEMORY.md's carry-over note). Row-major to
        // match every other PSO in this file (the same flag DistributeCascades is passed
        // per-call in BeginShadowPass; here it's a construction-time CreateInfo field
        // instead).
        CoordinateGridRenderer::CreateInfo gridCI;
        gridCI.PackMatrixRowMajor = true;
        m_impl->gridRenderer = std::make_unique<CoordinateGridRenderer>(m_impl->device, gridCI);
        return m_impl->gridRenderer != nullptr;
    }

    // Transparent textured-quad pipeline (roadmap #13: 2D & sprites). Same 3-target HDR
    // G-buffer shape as the toon/sky PSOs (the sprite pass runs while BeginFrame's targets
    // are still bound, between the opaque loop and EndScene -- see DrawSprite), but with
    // depth WRITES off (test stays on) and alpha blending enabled on color only: the
    // normal/motion targets are write-masked to COLOR_MASK_NONE so an unlit, blended sprite
    // never corrupts the SSAO/motion G-buffer the way writing zeros there (like the sky's
    // fullscreen PSO, which fully covers the frame and owns those pixels outright) would.
    bool Renderer::CreateSpritePipeline() {
        IRenderDevice *device = m_impl->device;
        IRenderStateCache *cache = m_impl->stateCache; // roadmap #10: shaders/PSOs route through this, not `device`

        {
            BufferDesc cbDesc;
            cbDesc.Name = "sprite constants";
            cbDesc.Size = sizeof(SpriteConstants);
            cbDesc.Usage = USAGE_DYNAMIC;
            cbDesc.BindFlags = BIND_UNIFORM_BUFFER;
            cbDesc.CPUAccessFlags = CPU_ACCESS_WRITE;
            device->CreateBuffer(cbDesc, nullptr, &m_impl->spriteConstants);
            if (!m_impl->spriteConstants) { return false; }
        }
        {
            // Shared unit quad (pos+uv), -0.5..0.5 in XY, Z=0: every sprite draw reuses this
            // one buffer, only the constants (world/tint/UV-rect) differ (ToonEngineOld's
            // gSpriteQuad, ported). Two triangles, CCW when seen from +Z, matching this
            // engine's front-face winding (RasterizerDesc.FrontCounterClockwise below).
            //
            // V is flipped relative to the old engine's OpenGL quad: OpenGL's texture origin
            // is bottom-left (V=0 = bottom row), so gSpriteQuad mapped the quad's bottom edge
            // to V=0; Diligent/Vulkan (like D3D) has a TOP-left origin -- CreateTextureFromFile
            // uploads the source image's row 0 (its top row) to V=0 -- so the same V=0-at-
            // bottom mapping here would show the image upside down. Bottom edge -> V=1, top
            // edge -> V=0 is what actually reproduces the source image right-side up.
            // clang-format off
            const float quad[] = {
                -0.5f, -0.5f, 0.0f,  0.0f, 1.0f,
                 0.5f, -0.5f, 0.0f,  1.0f, 1.0f,
                 0.5f,  0.5f, 0.0f,  1.0f, 0.0f,
                -0.5f, -0.5f, 0.0f,  0.0f, 1.0f,
                 0.5f,  0.5f, 0.0f,  1.0f, 0.0f,
                -0.5f,  0.5f, 0.0f,  0.0f, 0.0f,
            };
            // clang-format on
            BufferDesc vbDesc;
            vbDesc.Name = "sprite quad VB";
            vbDesc.Usage = USAGE_IMMUTABLE;
            vbDesc.BindFlags = BIND_VERTEX_BUFFER;
            vbDesc.Size = sizeof(quad);
            BufferData vbData{quad, vbDesc.Size};
            device->CreateBuffer(vbDesc, &vbData, &m_impl->spriteQuadVB);
            if (!m_impl->spriteQuadVB) { return false; }
        }

        IShaderSourceInputStreamFactory *sf = m_impl->shaderFactory;
        auto vs = CreateToonShader(cache, sf, SHADER_TYPE_VERTEX, "sprite VS", "sprite.hlsl", "VSMain");
        auto ps = CreateToonShader(cache, sf, SHADER_TYPE_PIXEL, "sprite PS", "sprite.hlsl", "PSMain");
        if (!vs || !ps) { return false; }

        LayoutElement layoutElems[] = {
            LayoutElement{0, 0, 3, VT_FLOAT32, False}, // ATTRIB0 position
            LayoutElement{1, 0, 2, VT_FLOAT32, False}, // ATTRIB1 uv
        };

        GraphicsPipelineStateCreateInfo ci;
        ci.PSODesc.Name = "sprite PSO";
        ci.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;

        GraphicsPipelineDesc &gp = ci.GraphicsPipeline;
        gp.NumRenderTargets = 3;
        gp.RTVFormats[0] = kHDRFormat;
        gp.RTVFormats[1] = kNormalFormat;
        gp.RTVFormats[2] = kMotionFormat;
        gp.DSVFormat = kSceneDepthFormat;
        gp.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        gp.RasterizerDesc.CullMode = CULL_MODE_NONE; // a sprite is visible from both sides
        gp.DepthStencilDesc.DepthEnable = True; // occluded by opaque geometry in front
        // WRITES depth (unlike a typical alpha-blended pass): Renderer::DrawGrid runs after
        // EndScene and occludes ITSELF by sampling the finished scene depth as an SRV, not by
        // depth-testing its own draw -- a sprite that never wrote depth was invisible to that
        // check, so the editor grid drew straight through every sprite regardless of its
        // actual opacity (roadmap #13 post-ship fix). The back-to-front sort (DrawSprite's
        // caller) still gives correct sprite-over-sprite compositing with writes on -- a
        // nearer sprite's LESS_EQUAL test still passes over a farther one's already-written
        // depth. The trade-off: a genuinely translucent (not just alpha-tested) sprite now
        // occludes the grid/SSAO/TAA/DoF/SSR at its OWN depth rather than the true depth
        // behind it, same as any other depth-writing partially-transparent surface.
        gp.DepthStencilDesc.DepthWriteEnable = True;
        gp.DepthStencilDesc.DepthFunc = COMPARISON_FUNC_LESS_EQUAL;

        // Alpha-blend on the COLOR target only; the normal/motion targets stay BlendEnable =
        // False (plain overwrite), which is exactly what they need: sprite.hlsl writes the
        // quad's REAL facing normal (+ roughness 1 = matte in .w) and REAL motion vector.
        // The first version write-masked RT1/RT2 off instead, and that was the "I can see
        // objects through the sprite" bug: the OCCLUDED object's normals/roughness/motion
        // stayed in the G-buffer under every sprite pixel, so the screen-space effects that
        // read those buffers at resolve time (SSR above all -- the ground's 0.05 roughness
        // made it paint reflections of the scene onto sprites covering it -- plus SSAO and
        // TAA/temporal reprojection) composited the hidden surface's lighting response on
        // top of the sprite. Same "depth-unaware later pass" failure shape as the
        // DepthWriteEnable fix above, one abstraction level up.
        gp.BlendDesc.IndependentBlendEnable = True;
        RenderTargetBlendDesc &rt0 = gp.BlendDesc.RenderTargets[0];
        rt0.BlendEnable = True;
        rt0.SrcBlend = BLEND_FACTOR_SRC_ALPHA;
        rt0.DestBlend = BLEND_FACTOR_INV_SRC_ALPHA;
        rt0.BlendOp = BLEND_OPERATION_ADD;
        rt0.SrcBlendAlpha = BLEND_FACTOR_ONE;
        rt0.DestBlendAlpha = BLEND_FACTOR_INV_SRC_ALPHA;
        rt0.BlendOpAlpha = BLEND_OPERATION_ADD;

        gp.InputLayout.LayoutElements = layoutElems;
        gp.InputLayout.NumElements = sizeof(layoutElems) / sizeof(layoutElems[0]);

        ci.pVS = vs;
        ci.pPS = ps;

        // Constants is shared STATIC (set once below); g_SpriteTex is DYNAMIC -- DrawSprite
        // re-Sets it per sprite on the one shared spriteSRB, the same pattern model_fill's
        // g_Albedo/modelSRB uses for a per-draw-varying texture (see CreateModelPipeline).
        ShaderResourceVariableDesc vars[] = {
            {SHADER_TYPE_VERTEX, "Constants", SHADER_RESOURCE_VARIABLE_TYPE_STATIC},
            {SHADER_TYPE_PIXEL, "Constants", SHADER_RESOURCE_VARIABLE_TYPE_STATIC},
            {SHADER_TYPE_PIXEL, "g_SpriteTex", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
        };
        ci.PSODesc.ResourceLayout.Variables = vars;
        ci.PSODesc.ResourceLayout.NumVariables = sizeof(vars) / sizeof(vars[0]);

        // CLAMP, not model_fill's WRAP: a sprite's UV rect is often an atlas sub-rect, and
        // wrapping would bleed the filter into a neighboring cell at the rect's edge under
        // linear filtering.
        SamplerDesc linClamp;
        linClamp.MinFilter = FILTER_TYPE_LINEAR;
        linClamp.MagFilter = FILTER_TYPE_LINEAR;
        linClamp.MipFilter = FILTER_TYPE_LINEAR;
        linClamp.AddressU = TEXTURE_ADDRESS_CLAMP;
        linClamp.AddressV = TEXTURE_ADDRESS_CLAMP;
        linClamp.AddressW = TEXTURE_ADDRESS_CLAMP;
        ImmutableSamplerDesc immSamplers[] = {
            {SHADER_TYPE_PIXEL, "g_SpriteTex", linClamp},
        };
        ci.PSODesc.ResourceLayout.ImmutableSamplers = immSamplers;
        ci.PSODesc.ResourceLayout.NumImmutableSamplers = sizeof(immSamplers) / sizeof(immSamplers[0]);

        cache->CreateGraphicsPipelineState(ci, &m_impl->spritePSO);
        if (!m_impl->spritePSO) { return false; }
        if (auto *v = m_impl->spritePSO->GetStaticVariableByName(SHADER_TYPE_VERTEX, "Constants")) {
            v->Set(m_impl->spriteConstants);
        }
        if (auto *p = m_impl->spritePSO->GetStaticVariableByName(SHADER_TYPE_PIXEL, "Constants")) {
            p->Set(m_impl->spriteConstants);
        }
        m_impl->spritePSO->CreateShaderResourceBinding(&m_impl->spriteSRB, true);
        return m_impl->spriteSRB != nullptr;
    }

    // In-game UI overlay pipeline (roadmap #17): screen-space tinted quads + MSDF text, drawn
    // straight onto the resolved back buffer after EndScene -- the same "1 RTV, no depth, alpha
    // blend" shape as the wireframe/ImGui overlays, not the sprite's 3-target HDR G-buffer. One
    // dynamic VB (kMaxUIVertices) is remapped per DrawUI batch; g_UIAtlas is DYNAMIC on the
    // shared SRB (re-Set per call) with an immutable linear-clamp sampler for the atlas.
    bool Renderer::CreateUIPipeline() {
        IRenderDevice *device = m_impl->device;
        IRenderStateCache *cache = m_impl->stateCache; // roadmap #10: shaders/PSOs route through this
        const SwapChainDesc &sc = m_impl->swapChain->GetDesc();

        {
            BufferDesc cbDesc;
            cbDesc.Name = "ui constants";
            cbDesc.Size = sizeof(UIConstants);
            cbDesc.Usage = USAGE_DYNAMIC;
            cbDesc.BindFlags = BIND_UNIFORM_BUFFER;
            cbDesc.CPUAccessFlags = CPU_ACCESS_WRITE;
            device->CreateBuffer(cbDesc, nullptr, &m_impl->uiConstants);
            if (!m_impl->uiConstants) { return false; }
        }
        {
            BufferDesc vbDesc;
            vbDesc.Name = "ui VB";
            vbDesc.Usage = USAGE_DYNAMIC;
            vbDesc.BindFlags = BIND_VERTEX_BUFFER;
            vbDesc.CPUAccessFlags = CPU_ACCESS_WRITE;
            vbDesc.Size = static_cast<Uint64>(kMaxUIVertices) * sizeof(UIVertex);
            device->CreateBuffer(vbDesc, nullptr, &m_impl->uiVB);
            if (!m_impl->uiVB) { return false; }
        }
        {
            // 1x1 opaque white, bound as g_UIAtlas for a solid-only batch (DrawUI given no
            // atlas), so the PSO always has a valid SRV even before a font is loaded.
            const Uint32 white = 0xFFFFFFFFu;
            TextureDesc td;
            td.Name = "ui white 1x1";
            td.Type = RESOURCE_DIM_TEX_2D;
            td.Width = 1;
            td.Height = 1;
            td.Format = TEX_FORMAT_RGBA8_UNORM;
            td.BindFlags = BIND_SHADER_RESOURCE;
            td.Usage = USAGE_IMMUTABLE;
            TextureSubResData sub{&white, sizeof(white)};
            TextureData init{&sub, 1};
            device->CreateTexture(td, &init, &m_impl->uiWhiteTex);
            if (!m_impl->uiWhiteTex) { return false; }
        }

        IShaderSourceInputStreamFactory *sf = m_impl->shaderFactory;
        auto vs = CreateToonShader(cache, sf, SHADER_TYPE_VERTEX, "ui VS", "ui.hlsl", "VSMain");
        auto ps = CreateToonShader(cache, sf, SHADER_TYPE_PIXEL, "ui PS", "ui.hlsl", "PSMain");
        if (!vs || !ps) { return false; }

        LayoutElement layoutElems[] = {
            LayoutElement{0, 0, 2, VT_FLOAT32, False}, // ATTRIB0 pos (pixels)
            LayoutElement{1, 0, 2, VT_FLOAT32, False}, // ATTRIB1 uv
            LayoutElement{2, 0, 4, VT_FLOAT32, False}, // ATTRIB2 color
            LayoutElement{3, 0, 1, VT_FLOAT32, False}, // ATTRIB3 mode
            LayoutElement{4, 0, 4, VT_FLOAT32, False}, // ATTRIB4 params (rounded rect)
            LayoutElement{5, 0, 4, VT_FLOAT32, False}, // ATTRIB5 borderColor (rounded rect)
        };

        GraphicsPipelineStateCreateInfo ci;
        ci.PSODesc.Name = "ui PSO";
        ci.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;

        GraphicsPipelineDesc &gp = ci.GraphicsPipeline;
        gp.NumRenderTargets = 1;
        gp.RTVFormats[0] = sc.ColorBufferFormat; // resolved back buffer, same as wireframe/tonemap
        gp.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        gp.RasterizerDesc.CullMode = CULL_MODE_NONE; // 2D quads: winding-agnostic
        gp.DepthStencilDesc.DepthEnable = False;     // an overlay: no depth, always on top
        gp.InputLayout.LayoutElements = layoutElems;
        gp.InputLayout.NumElements = sizeof(layoutElems) / sizeof(layoutElems[0]);

        // Straight-alpha blend (SrcAlpha/InvSrcAlpha), the same as sprite RT0 and what ui.hlsl's
        // solid + text outputs both expect.
        RenderTargetBlendDesc &rt0 = gp.BlendDesc.RenderTargets[0];
        rt0.BlendEnable = True;
        rt0.SrcBlend = BLEND_FACTOR_SRC_ALPHA;
        rt0.DestBlend = BLEND_FACTOR_INV_SRC_ALPHA;
        rt0.BlendOp = BLEND_OPERATION_ADD;
        rt0.SrcBlendAlpha = BLEND_FACTOR_ONE;
        rt0.DestBlendAlpha = BLEND_FACTOR_INV_SRC_ALPHA;
        rt0.BlendOpAlpha = BLEND_OPERATION_ADD;

        ci.pVS = vs;
        ci.pPS = ps;

        // Constants is shared STATIC (VS reads screenSize, PS reads atlasSize/pixelRange, so
        // each stage binds its own copy -- same as wireframe/sprite); g_UIAtlas is DYNAMIC,
        // re-Set per DrawUI call on the one shared uiSRB.
        // g_UIAtlas is declared for BOTH stages: Diligent's combined-texture-sampler reflection
        // surfaces it in the VS resource signature too (even though only PSMain samples it), so it
        // must be bindable there as well or the draw fails validation ("no resource bound to
        // g_UIAtlas in ui VS") and is skipped. DrawUI binds both.
        ShaderResourceVariableDesc vars[] = {
            {SHADER_TYPE_VERTEX, "Constants", SHADER_RESOURCE_VARIABLE_TYPE_STATIC},
            {SHADER_TYPE_PIXEL, "Constants", SHADER_RESOURCE_VARIABLE_TYPE_STATIC},
            {SHADER_TYPE_VERTEX | SHADER_TYPE_PIXEL, "g_UIAtlas", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
        };
        ci.PSODesc.ResourceLayout.Variables = vars;
        ci.PSODesc.ResourceLayout.NumVariables = sizeof(vars) / sizeof(vars[0]);

        SamplerDesc linClamp;
        linClamp.MinFilter = FILTER_TYPE_LINEAR;
        linClamp.MagFilter = FILTER_TYPE_LINEAR;
        linClamp.MipFilter = FILTER_TYPE_LINEAR;
        linClamp.AddressU = TEXTURE_ADDRESS_CLAMP;
        linClamp.AddressV = TEXTURE_ADDRESS_CLAMP;
        linClamp.AddressW = TEXTURE_ADDRESS_CLAMP;
        ImmutableSamplerDesc immSamplers[] = {
            {SHADER_TYPE_VERTEX | SHADER_TYPE_PIXEL, "g_UIAtlas", linClamp},
        };
        ci.PSODesc.ResourceLayout.ImmutableSamplers = immSamplers;
        ci.PSODesc.ResourceLayout.NumImmutableSamplers = sizeof(immSamplers) / sizeof(immSamplers[0]);

        cache->CreateGraphicsPipelineState(ci, &m_impl->uiPSO);
        if (!m_impl->uiPSO) { return false; }
        if (auto *v = m_impl->uiPSO->GetStaticVariableByName(SHADER_TYPE_VERTEX, "Constants")) {
            v->Set(m_impl->uiConstants);
        }
        if (auto *p = m_impl->uiPSO->GetStaticVariableByName(SHADER_TYPE_PIXEL, "Constants")) {
            p->Set(m_impl->uiConstants);
        }
        m_impl->uiPSO->CreateShaderResourceBinding(&m_impl->uiSRB, true);
        return m_impl->uiSRB != nullptr;
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
        auto make1x1 = [&](const char *name, const Uint8 rgba[4], RefCntAutoPtr<ITexture> &out) {
            TextureSubResData sub{rgba, 4};
            TextureData texData{&sub, 1};
            TextureDesc td;
            td.Name = name;
            td.Type = RESOURCE_DIM_TEX_2D;
            td.Width = 1;
            td.Height = 1;
            td.MipLevels = 1;
            td.Format = TEX_FORMAT_RGBA8_UNORM;
            td.BindFlags = BIND_SHADER_RESOURCE;
            m_impl->device->CreateTexture(td, &texData, &out);
        };
        const Uint8 white[4] = {255, 255, 255, 255};
        const Uint8 black[4] = {0, 0, 0, 0};
        make1x1("AO white default", white, m_impl->aoWhite);
        make1x1("SSR black default", black, m_impl->ssrBlack);

        // 1x1 white 2D-ARRAY albedo default: the glTF loader stores textures as 2D arrays, so
        // the model fill's g_Albedo is a Texture2DArray; the untextured fallback must match
        // that dimension (the plain-2D aoWhite would trip a view-dimension assertion).
        {
            const Uint8 white1[4] = {255, 255, 255, 255};
            TextureSubResData sub{white1, 4};
            TextureData texData{&sub, 1};
            TextureDesc td;
            td.Name = "model albedo white default";
            td.Type = RESOURCE_DIM_TEX_2D_ARRAY;
            td.Width = 1;
            td.Height = 1;
            td.ArraySize = 1;
            td.MipLevels = 1;
            td.Format = TEX_FORMAT_RGBA8_UNORM;
            td.BindFlags = BIND_SHADER_RESOURCE;
            m_impl->device->CreateTexture(td, &texData, &m_impl->modelWhite);
        }

        return m_impl->postFX && m_impl->bloom && m_impl->ssao && m_impl->dof && m_impl->taa && m_impl->ssr &&
               m_impl->aoWhite && m_impl->ssrBlack && m_impl->modelWhite;
    }

    bool Renderer::CreateToonPipeline() {
        IRenderDevice *device = m_impl->device;
        IRenderStateCache *cache = m_impl->stateCache; // roadmap #10: shaders/PSOs route through this, not `device`

        // Shared, per-draw-updated constant buffer.
        {
            BufferDesc cbDesc;
            cbDesc.Name = "toon constants";
            cbDesc.Size = sizeof(ShaderConstants);
            cbDesc.Usage = USAGE_DYNAMIC;
            cbDesc.BindFlags = BIND_UNIFORM_BUFFER;
            cbDesc.CPUAccessFlags = CPU_ACCESS_WRITE;
            device->CreateBuffer(cbDesc, nullptr, &m_impl->constants);
            if (!m_impl->constants) { return false; }
        }

        // Vertex input layout: mirrors toon::Vertex (two tightly-packed float3s;
        // offsets/stride auto-computed from element order).
        LayoutElement layoutElems[] = {
            LayoutElement{0, 0, 3, VT_FLOAT32, False}, // ATTRIB0 position
            LayoutElement{1, 0, 3, VT_FLOAT32, False}, // ATTRIB1 normal (shading)
            LayoutElement{2, 0, 3, VT_FLOAT32, False}, // ATTRIB2 smooth normal (outline hull)
        };

        // Build a graphics PSO for one toon pass and wire the shared CB into it.
        auto buildPass = [&](const char *name, IShader *vs, IShader *ps, CULL_MODE cull,
                             RefCntAutoPtr<IPipelineState> &pso, RefCntAutoPtr<IShaderResourceBinding> &srb) -> bool {
            GraphicsPipelineStateCreateInfo ci;
            ci.PSODesc.Name = name;
            ci.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;

            GraphicsPipelineDesc &gp = ci.GraphicsPipeline;
            gp.NumRenderTargets = 3;          // MRT: color + normals + motion
            gp.RTVFormats[0] = kHDRFormat;    // SV_Target0: HDR scene color
            gp.RTVFormats[1] = kNormalFormat; // SV_Target1: world-space normals
            gp.RTVFormats[2] = kMotionFormat; // SV_Target2: NDC motion vectors
            gp.DSVFormat = kSceneDepthFormat;
            gp.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            gp.RasterizerDesc.CullMode = cull;
            gp.RasterizerDesc.FrontCounterClockwise = True; // primitives are CCW when seen from outside
            gp.DepthStencilDesc.DepthEnable = True;
            gp.DepthStencilDesc.DepthWriteEnable = True;
            gp.DepthStencilDesc.DepthFunc = COMPARISON_FUNC_LESS_EQUAL;
            gp.InputLayout.LayoutElements = layoutElems;
            gp.InputLayout.NumElements = sizeof(layoutElems) / sizeof(layoutElems[0]);

            ci.pVS = vs;
            ci.pPS = ps;
            // One shared static CB across both stages; set once on the PSO below.
            ci.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;

            cache->CreateGraphicsPipelineState(ci, &pso);
            if (!pso) { return false; }
            if (auto *v = pso->GetStaticVariableByName(SHADER_TYPE_VERTEX, "Constants")) { v->Set(m_impl->constants); }
            if (auto *p = pso->GetStaticVariableByName(SHADER_TYPE_PIXEL, "Constants")) { p->Set(m_impl->constants); }
            // Shadow-map inputs: only toon_fill.hlsl's PS actually declares these (the
            // outline pass never calls ComputeShadowFactor), so GetStaticVariableByName
            // returns null there and these are harmlessly skipped -- same "set if present"
            // pattern as Constants above, just gracefully absent on the outline PSO. The
            // sampler is NOT bound here -- combined-sampler mode attaches it to the shadow
            // map's texture VIEW directly (see CreateShadowMap's SetSampler call), not as a
            // separate SRB/PSO variable.
            if (auto *v = pso->GetStaticVariableByName(SHADER_TYPE_PIXEL, "ShadowAttribsCB")) {
                v->Set(m_impl->shadowAttribsCB);
            }
            if (auto *v = pso->GetStaticVariableByName(SHADER_TYPE_PIXEL, "g_ShadowMap")) {
                v->Set(m_impl->shadowMap.GetSRV());
            }
            pso->CreateShaderResourceBinding(&srb, true);
            return srb != nullptr;
        };

        IShaderSourceInputStreamFactory *sf = m_impl->shaderFactory;

        // Fill shaders alone need PACK_MATRIX_ROW_MAJOR (see CreateToonShader's comment) --
        // they're the only ones that reference DiligentFX's ShadowMapAttribs/CascadeAttribs.
        auto fillVS = CreateToonShader(cache, sf, SHADER_TYPE_VERTEX, "toon fill VS", "toon_fill.hlsl", "VSMain",
                                       SHADER_COMPILE_FLAG_PACK_MATRIX_ROW_MAJOR);
        auto fillPS = CreateToonShader(cache, sf, SHADER_TYPE_PIXEL, "toon fill PS", "toon_fill.hlsl", "PSMain",
                                       SHADER_COMPILE_FLAG_PACK_MATRIX_ROW_MAJOR);
        auto outVS = CreateToonShader(cache, sf, SHADER_TYPE_VERTEX, "toon outline VS", "toon_outline.hlsl", "VSMain");
        auto outPS = CreateToonShader(cache, sf, SHADER_TYPE_PIXEL, "toon outline PS", "toon_outline.hlsl", "PSMain");
        if (!fillVS || !fillPS || !outVS || !outPS) { return false; }

        // Fill: cull back faces. Outline: cull front faces (keep the enlarged shell).
        if (!buildPass("toon fill PSO", fillVS, fillPS, CULL_MODE_BACK, m_impl->fillPSO, m_impl->fillSRB)) {
            return false;
        }
        if (!buildPass("toon outline PSO", outVS, outPS, CULL_MODE_FRONT, m_impl->outlinePSO, m_impl->outlineSRB)) {
            return false;
        }
        return true;
    }

    bool Renderer::CreateModelPipeline() {
        IRenderDevice *device = m_impl->device;
        IRenderStateCache *cache = m_impl->stateCache; // roadmap #10: shaders/PSOs route through this, not `device`
        IShaderSourceInputStreamFactory *sf = m_impl->shaderFactory;

        // PACK_MATRIX_ROW_MAJOR: model_fill.hlsl also references DiligentFX's ShadowMapAttribs/
        // CascadeAttribs (via ComputeShadowFactor) -- see CreateToonShader's comment.
        auto vs = CreateToonShader(cache, sf, SHADER_TYPE_VERTEX, "model fill VS", "model_fill.hlsl", "VSMain",
                                   SHADER_COMPILE_FLAG_PACK_MATRIX_ROW_MAJOR);
        auto ps = CreateToonShader(cache, sf, SHADER_TYPE_PIXEL, "model fill PS", "model_fill.hlsl", "PSMain",
                                   SHADER_COMPILE_FLAG_PACK_MATRIX_ROW_MAJOR);
        if (!vs || !ps) { return false; }

        // Input layout matching the interleaved buffer the loader fills from ModelVertexAttribs
        // (POSITION/NORMAL/TEXCOORD_0 in buffer 0). Auto offset/stride reproduce the loader's
        // packing exactly (pos@0, normal@12, uv@24, stride 32), so we hardcode it: same idiom
        // as CreateToonPipeline.
        LayoutElement modelLayout[] = {
            LayoutElement{0, 0, 3, VT_FLOAT32, False}, // ATTRIB0 position
            LayoutElement{1, 0, 3, VT_FLOAT32, False}, // ATTRIB1 normal
            LayoutElement{2, 0, 2, VT_FLOAT32, False}, // ATTRIB2 uv
        };

        GraphicsPipelineStateCreateInfo ci;
        ci.PSODesc.Name = "model fill PSO";
        ci.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;

        GraphicsPipelineDesc &gp = ci.GraphicsPipeline;
        gp.NumRenderTargets = 3; // MRT: color + normals + motion (same as toon)
        gp.RTVFormats[0] = kHDRFormat;
        gp.RTVFormats[1] = kNormalFormat;
        gp.RTVFormats[2] = kMotionFormat;
        gp.DSVFormat = kSceneDepthFormat;
        gp.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        gp.RasterizerDesc.CullMode = CULL_MODE_BACK;
        // glTF winds front faces CCW in its RIGHT-handed space; our projection is LEFT-handed,
        // which flips screen-space winding, so the model's outward faces are CW here. Hence
        // FrontCounterClockwise = False: the OPPOSITE of our own primitives (authored CCW-front
        // for the LH setup). With True, the outward faces cull and you see through the helmet to
        // its inner surface.
        gp.RasterizerDesc.FrontCounterClockwise = False;
        gp.DepthStencilDesc.DepthEnable = True;
        gp.DepthStencilDesc.DepthWriteEnable = True;
        gp.DepthStencilDesc.DepthFunc = COMPARISON_FUNC_LESS_EQUAL;
        gp.InputLayout.LayoutElements = modelLayout;
        gp.InputLayout.NumElements = sizeof(modelLayout) / sizeof(modelLayout[0]);

        ci.pVS = vs;
        ci.pPS = ps;

        // Shared static Constants CB (like the toon PSOs); g_Albedo is DYNAMIC: DrawModel
        // re-Sets it per primitive, with a linear-wrap immutable sampler (combined-sampler
        // "g_Albedo", same pattern as tonemap's g_HDRColor). ShadowAttribsCB/g_ShadowMap are
        // STATIC (set once below, like Constants) -- model_fill.hlsl's ComputeShadowFactor
        // call is what pulls these into this PSO's reflected resources. No separate
        // g_ShadowMap_sampler entry: combined-sampler mode attaches that sampler to the
        // shadow map's texture VIEW directly (CreateShadowMap's SetSampler call), not as an
        // SRB/PSO variable.
        ShaderResourceVariableDesc vars[] = {
            {SHADER_TYPE_VERTEX, "Constants", SHADER_RESOURCE_VARIABLE_TYPE_STATIC},
            {SHADER_TYPE_PIXEL, "Constants", SHADER_RESOURCE_VARIABLE_TYPE_STATIC},
            {SHADER_TYPE_PIXEL, "g_Albedo", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
            {SHADER_TYPE_PIXEL, "ShadowAttribsCB", SHADER_RESOURCE_VARIABLE_TYPE_STATIC},
            {SHADER_TYPE_PIXEL, "g_ShadowMap", SHADER_RESOURCE_VARIABLE_TYPE_STATIC},
        };
        ci.PSODesc.ResourceLayout.Variables = vars;
        ci.PSODesc.ResourceLayout.NumVariables = sizeof(vars) / sizeof(vars[0]);

        SamplerDesc linWrap;
        linWrap.MinFilter = FILTER_TYPE_LINEAR;
        linWrap.MagFilter = FILTER_TYPE_LINEAR;
        linWrap.MipFilter = FILTER_TYPE_LINEAR;
        linWrap.AddressU = TEXTURE_ADDRESS_WRAP;
        linWrap.AddressV = TEXTURE_ADDRESS_WRAP;
        linWrap.AddressW = TEXTURE_ADDRESS_WRAP;
        ImmutableSamplerDesc immSamplers[] = {
            {SHADER_TYPE_PIXEL, "g_Albedo", linWrap},
        };
        ci.PSODesc.ResourceLayout.ImmutableSamplers = immSamplers;
        ci.PSODesc.ResourceLayout.NumImmutableSamplers = sizeof(immSamplers) / sizeof(immSamplers[0]);

        cache->CreateGraphicsPipelineState(ci, &m_impl->modelPSO);
        if (!m_impl->modelPSO) { return false; }
        if (auto *v = m_impl->modelPSO->GetStaticVariableByName(SHADER_TYPE_VERTEX, "Constants")) {
            v->Set(m_impl->constants);
        }
        if (auto *p = m_impl->modelPSO->GetStaticVariableByName(SHADER_TYPE_PIXEL, "Constants")) {
            p->Set(m_impl->constants);
        }
        if (auto *v = m_impl->modelPSO->GetStaticVariableByName(SHADER_TYPE_PIXEL, "ShadowAttribsCB")) {
            v->Set(m_impl->shadowAttribsCB);
        }
        if (auto *v = m_impl->modelPSO->GetStaticVariableByName(SHADER_TYPE_PIXEL, "g_ShadowMap")) {
            v->Set(m_impl->shadowMap.GetSRV());
        }
        m_impl->modelPSO->CreateShaderResourceBinding(&m_impl->modelSRB, true);
        if (!m_impl->modelSRB) { return false; }

        // --- Outline pass PSO (inverted hull) --- same vertex layout; cull FRONT to keep the
        // enlarged back-facing shell; only the shared Constants CB (no albedo texture).
        auto ovs = CreateToonShader(cache, sf, SHADER_TYPE_VERTEX, "model outline VS", "model_outline.hlsl", "VSMain");
        auto ops = CreateToonShader(cache, sf, SHADER_TYPE_PIXEL, "model outline PS", "model_outline.hlsl", "PSMain");
        if (!ovs || !ops) { return false; }

        GraphicsPipelineStateCreateInfo oci;
        oci.PSODesc.Name = "model outline PSO";
        oci.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;

        GraphicsPipelineDesc &ogp = oci.GraphicsPipeline;
        ogp.NumRenderTargets = 3;
        ogp.RTVFormats[0] = kHDRFormat;
        ogp.RTVFormats[1] = kNormalFormat;
        ogp.RTVFormats[2] = kMotionFormat;
        ogp.DSVFormat = kSceneDepthFormat;
        ogp.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        ogp.RasterizerDesc.CullMode = CULL_MODE_FRONT;    // keep the enlarged back shell
        ogp.RasterizerDesc.FrontCounterClockwise = False; // model winding (matches the fill)
        ogp.DepthStencilDesc.DepthEnable = True;
        ogp.DepthStencilDesc.DepthWriteEnable = True;
        ogp.DepthStencilDesc.DepthFunc = COMPARISON_FUNC_LESS_EQUAL;
        ogp.InputLayout.LayoutElements = modelLayout;
        ogp.InputLayout.NumElements = sizeof(modelLayout) / sizeof(modelLayout[0]);

        oci.pVS = ovs;
        oci.pPS = ops;

        ShaderResourceVariableDesc ovars[] = {
            {SHADER_TYPE_VERTEX, "Constants", SHADER_RESOURCE_VARIABLE_TYPE_STATIC},
            {SHADER_TYPE_PIXEL, "Constants", SHADER_RESOURCE_VARIABLE_TYPE_STATIC},
        };
        oci.PSODesc.ResourceLayout.Variables = ovars;
        oci.PSODesc.ResourceLayout.NumVariables = sizeof(ovars) / sizeof(ovars[0]);

        cache->CreateGraphicsPipelineState(oci, &m_impl->modelOutlinePSO);
        if (!m_impl->modelOutlinePSO) { return false; }
        if (auto *v = m_impl->modelOutlinePSO->GetStaticVariableByName(SHADER_TYPE_VERTEX, "Constants")) {
            v->Set(m_impl->constants);
        }
        if (auto *p = m_impl->modelOutlinePSO->GetStaticVariableByName(SHADER_TYPE_PIXEL, "Constants")) {
            p->Set(m_impl->constants);
        }
        m_impl->modelOutlinePSO->CreateShaderResourceBinding(&m_impl->modelOutlineSRB, true);
        return m_impl->modelOutlineSRB != nullptr;
    }

    // Roadmap #11 (skeletal animation): animated-model counterpart of CreateModelPipeline,
    // for models with at least one glTF skin (see Renderer::ModelHasSkin). A separate pair
    // of PSOs, not a runtime branch added to the static ones -- their vertex layout genuinely
    // differs (JOINTS_0/WEIGHTS_0 in a second buffer slot), the same reason the shadow pass
    // already keeps separate procedural-vs-model PSOs. DrawModel/DrawModelShadow pick these
    // per node (GLTF::Node::pSkin != nullptr), falling back to the static PSOs otherwise, so
    // an unskinned model's draw path is entirely unaffected by this function's existence.
    bool Renderer::CreateSkinnedModelPipeline() {
        IRenderDevice *device = m_impl->device;
        IRenderStateCache *cache = m_impl->stateCache; // roadmap #10: shaders/PSOs route through this, not `device`
        IShaderSourceInputStreamFactory *sf = m_impl->shaderFactory;

        // Small per-draw scalar (how many joints this draw's palette holds, for the fill/
        // outline shaders' current-vs-previous offset math -- see SkinConstants's own
        // comment). Same MapHelper-remapped-per-draw idiom as every other CB here.
        {
            BufferDesc cbDesc;
            cbDesc.Name = "skin constants";
            cbDesc.Size = sizeof(SkinConstants);
            cbDesc.Usage = USAGE_DYNAMIC;
            cbDesc.BindFlags = BIND_UNIFORM_BUFFER;
            cbDesc.CPUAccessFlags = CPU_ACCESS_WRITE;
            device->CreateBuffer(cbDesc, nullptr, &m_impl->skinConstants);
            if (!m_impl->skinConstants) { return false; }
        }

        // Buffer 0: pos/normal/uv, byte-identical to modelLayout in CreateModelPipeline
        // (pos@0/normal@12/uv@24, stride 32). Buffer 1: JOINTS_0/WEIGHTS_0 interleaved
        // (joints@0/weights@16, stride 32) -- matches the loader's own packing for the
        // ModelVertexAttribs entries requesting buffer slot 1.
        LayoutElement skinnedLayout[] = {
            LayoutElement{0, 0, 3, VT_FLOAT32, False}, // ATTRIB0 position (buffer 0)
            LayoutElement{1, 0, 3, VT_FLOAT32, False}, // ATTRIB1 normal (buffer 0)
            LayoutElement{2, 0, 2, VT_FLOAT32, False}, // ATTRIB2 uv (buffer 0)
            LayoutElement{3, 1, 4, VT_FLOAT32, False}, // ATTRIB3 joints (buffer 1)
            LayoutElement{4, 1, 4, VT_FLOAT32, False}, // ATTRIB4 weights (buffer 1)
        };

        // --- Fill pass PSO ---
        // PACK_MATRIX_ROW_MAJOR: model_fill_skinned.hlsl also references DiligentFX's
        // ShadowMapAttribs/CascadeAttribs (via ComputeShadowFactor) -- see CreateToonShader's
        // comment.
        auto vs = CreateToonShader(cache, sf, SHADER_TYPE_VERTEX, "model fill skinned VS", "model_fill_skinned.hlsl",
                                   "VSMain", SHADER_COMPILE_FLAG_PACK_MATRIX_ROW_MAJOR);
        auto ps = CreateToonShader(cache, sf, SHADER_TYPE_PIXEL, "model fill skinned PS", "model_fill_skinned.hlsl",
                                   "PSMain", SHADER_COMPILE_FLAG_PACK_MATRIX_ROW_MAJOR);
        if (!vs || !ps) { return false; }

        GraphicsPipelineStateCreateInfo ci;
        ci.PSODesc.Name = "model fill skinned PSO";
        ci.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;

        GraphicsPipelineDesc &gp = ci.GraphicsPipeline;
        gp.NumRenderTargets = 3;
        gp.RTVFormats[0] = kHDRFormat;
        gp.RTVFormats[1] = kNormalFormat;
        gp.RTVFormats[2] = kMotionFormat;
        gp.DSVFormat = kSceneDepthFormat;
        gp.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        gp.RasterizerDesc.CullMode = CULL_MODE_BACK;
        gp.RasterizerDesc.FrontCounterClockwise = False; // model winding, same as CreateModelPipeline
        gp.DepthStencilDesc.DepthEnable = True;
        gp.DepthStencilDesc.DepthWriteEnable = True;
        gp.DepthStencilDesc.DepthFunc = COMPARISON_FUNC_LESS_EQUAL;
        gp.InputLayout.LayoutElements = skinnedLayout;
        gp.InputLayout.NumElements = sizeof(skinnedLayout) / sizeof(skinnedLayout[0]);

        ci.pVS = vs;
        ci.pPS = ps;

        // Same shape as CreateModelPipeline's `vars`, plus SkinConstants (STATIC: bound once
        // below, remapped via MapHelper per draw thereafter, like Constants) and g_Joints
        // (DYNAMIC: its buffer doesn't exist yet at PSO-creation time and can be recreated
        // later on growth -- see EnsureJointsBufferCapacity's own comment).
        ShaderResourceVariableDesc vars[] = {
            {SHADER_TYPE_VERTEX, "Constants", SHADER_RESOURCE_VARIABLE_TYPE_STATIC},
            {SHADER_TYPE_PIXEL, "Constants", SHADER_RESOURCE_VARIABLE_TYPE_STATIC},
            {SHADER_TYPE_PIXEL, "g_Albedo", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
            {SHADER_TYPE_PIXEL, "ShadowAttribsCB", SHADER_RESOURCE_VARIABLE_TYPE_STATIC},
            {SHADER_TYPE_PIXEL, "g_ShadowMap", SHADER_RESOURCE_VARIABLE_TYPE_STATIC},
            {SHADER_TYPE_VERTEX, "SkinConstants", SHADER_RESOURCE_VARIABLE_TYPE_STATIC},
            {SHADER_TYPE_VERTEX, "g_Joints", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
        };
        ci.PSODesc.ResourceLayout.Variables = vars;
        ci.PSODesc.ResourceLayout.NumVariables = sizeof(vars) / sizeof(vars[0]);

        SamplerDesc linWrap;
        linWrap.MinFilter = FILTER_TYPE_LINEAR;
        linWrap.MagFilter = FILTER_TYPE_LINEAR;
        linWrap.MipFilter = FILTER_TYPE_LINEAR;
        linWrap.AddressU = TEXTURE_ADDRESS_WRAP;
        linWrap.AddressV = TEXTURE_ADDRESS_WRAP;
        linWrap.AddressW = TEXTURE_ADDRESS_WRAP;
        ImmutableSamplerDesc immSamplers[] = {
            {SHADER_TYPE_PIXEL, "g_Albedo", linWrap},
        };
        ci.PSODesc.ResourceLayout.ImmutableSamplers = immSamplers;
        ci.PSODesc.ResourceLayout.NumImmutableSamplers = sizeof(immSamplers) / sizeof(immSamplers[0]);

        cache->CreateGraphicsPipelineState(ci, &m_impl->modelSkinnedPSO);
        if (!m_impl->modelSkinnedPSO) { return false; }
        if (auto *v = m_impl->modelSkinnedPSO->GetStaticVariableByName(SHADER_TYPE_VERTEX, "Constants")) {
            v->Set(m_impl->constants);
        }
        if (auto *p = m_impl->modelSkinnedPSO->GetStaticVariableByName(SHADER_TYPE_PIXEL, "Constants")) {
            p->Set(m_impl->constants);
        }
        if (auto *v = m_impl->modelSkinnedPSO->GetStaticVariableByName(SHADER_TYPE_PIXEL, "ShadowAttribsCB")) {
            v->Set(m_impl->shadowAttribsCB);
        }
        if (auto *v = m_impl->modelSkinnedPSO->GetStaticVariableByName(SHADER_TYPE_PIXEL, "g_ShadowMap")) {
            v->Set(m_impl->shadowMap.GetSRV());
        }
        if (auto *v = m_impl->modelSkinnedPSO->GetStaticVariableByName(SHADER_TYPE_VERTEX, "SkinConstants")) {
            v->Set(m_impl->skinConstants);
        }
        m_impl->modelSkinnedPSO->CreateShaderResourceBinding(&m_impl->modelSkinnedSRB, true);
        if (!m_impl->modelSkinnedSRB) { return false; }

        // --- Outline pass PSO (inverted hull) --- same skinned vertex layout; cull FRONT to
        // keep the enlarged back-facing shell; Constants + SkinConstants + g_Joints only (no
        // albedo texture, matching CreateModelPipeline's own unskinned outline PSO).
        // PACK_MATRIX_ROW_MAJOR: joints_common.hlsli's g_Joints needs it (see that file's own
        // comment) -- unlike model_fill_skinned.hlsl, this shader doesn't ALSO need it for
        // ShadowMapAttribs/CascadeAttribs (this pass never calls ComputeShadowFactor), but the
        // flag is still required for g_Joints alone.
        auto ovs = CreateToonShader(cache, sf, SHADER_TYPE_VERTEX, "model outline skinned VS",
                                    "model_outline_skinned.hlsl", "VSMain", SHADER_COMPILE_FLAG_PACK_MATRIX_ROW_MAJOR);
        auto ops = CreateToonShader(cache, sf, SHADER_TYPE_PIXEL, "model outline skinned PS",
                                    "model_outline_skinned.hlsl", "PSMain", SHADER_COMPILE_FLAG_PACK_MATRIX_ROW_MAJOR);
        if (!ovs || !ops) { return false; }

        GraphicsPipelineStateCreateInfo oci;
        oci.PSODesc.Name = "model outline skinned PSO";
        oci.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;

        GraphicsPipelineDesc &ogp = oci.GraphicsPipeline;
        ogp.NumRenderTargets = 3;
        ogp.RTVFormats[0] = kHDRFormat;
        ogp.RTVFormats[1] = kNormalFormat;
        ogp.RTVFormats[2] = kMotionFormat;
        ogp.DSVFormat = kSceneDepthFormat;
        ogp.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        ogp.RasterizerDesc.CullMode = CULL_MODE_FRONT;
        ogp.RasterizerDesc.FrontCounterClockwise = False;
        ogp.DepthStencilDesc.DepthEnable = True;
        ogp.DepthStencilDesc.DepthWriteEnable = True;
        ogp.DepthStencilDesc.DepthFunc = COMPARISON_FUNC_LESS_EQUAL;
        ogp.InputLayout.LayoutElements = skinnedLayout;
        ogp.InputLayout.NumElements = sizeof(skinnedLayout) / sizeof(skinnedLayout[0]);

        oci.pVS = ovs;
        oci.pPS = ops;

        ShaderResourceVariableDesc ovars[] = {
            {SHADER_TYPE_VERTEX, "Constants", SHADER_RESOURCE_VARIABLE_TYPE_STATIC},
            {SHADER_TYPE_PIXEL, "Constants", SHADER_RESOURCE_VARIABLE_TYPE_STATIC},
            {SHADER_TYPE_VERTEX, "SkinConstants", SHADER_RESOURCE_VARIABLE_TYPE_STATIC},
            {SHADER_TYPE_VERTEX, "g_Joints", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
        };
        oci.PSODesc.ResourceLayout.Variables = ovars;
        oci.PSODesc.ResourceLayout.NumVariables = sizeof(ovars) / sizeof(ovars[0]);

        cache->CreateGraphicsPipelineState(oci, &m_impl->modelOutlineSkinnedPSO);
        if (!m_impl->modelOutlineSkinnedPSO) { return false; }
        if (auto *v = m_impl->modelOutlineSkinnedPSO->GetStaticVariableByName(SHADER_TYPE_VERTEX, "Constants")) {
            v->Set(m_impl->constants);
        }
        if (auto *p = m_impl->modelOutlineSkinnedPSO->GetStaticVariableByName(SHADER_TYPE_PIXEL, "Constants")) {
            p->Set(m_impl->constants);
        }
        if (auto *v =
                m_impl->modelOutlineSkinnedPSO->GetStaticVariableByName(SHADER_TYPE_VERTEX, "SkinConstants")) {
            v->Set(m_impl->skinConstants);
        }
        m_impl->modelOutlineSkinnedPSO->CreateShaderResourceBinding(&m_impl->modelOutlineSkinnedSRB, true);
        return m_impl->modelOutlineSkinnedSRB != nullptr;
    }

    // --- Teardown ---------------------------------------------------------------

    void Renderer::Shutdown() {
        if (!m_impl) { return; }
        // Wait for the GPU to finish the last submitted frame before releasing anything.
        // Its commands may still be reading the HDR / bloom / depth targets; releasing
        // those while they're in flight trips Diligent's in-use checks and pops the
        // debug abort dialog on window close. WaitForIdle also flushes.
        if (m_impl->context) { m_impl->context->WaitForIdle(); }
        ShutdownUI(); // must release ImGui's GPU resources before the device

        // Release scene/pipeline GPU objects before the device.
        m_impl->meshes.clear();
        m_impl->models.clear();   // GLTF::Model objects own GPU buffers + textures
        m_impl->textures.clear(); // editor-UI thumbnails (asset browser)
        m_impl->bloom.reset();    // DiligentFX effect objects own GPU resources
        m_impl->ssao.reset();
        m_impl->dof.reset();
        m_impl->taa.reset();
        m_impl->ssr.reset();
        m_impl->postFX.reset();
        m_impl->gridRenderer.reset(); // DiligentFX component; owns its own PSO cache + SRB + CBs
        m_impl->motionVectors.Release();
        m_impl->aoWhite.Release();
        m_impl->ssrBlack.Release();
        m_impl->modelWhite.Release();
        m_impl->shadowSRB.Release();
        m_impl->shadowPSO.Release();
        m_impl->modelShadowSRB.Release();
        m_impl->modelShadowPSO.Release();
        m_impl->shadowSampler.Release();
        m_impl->shadowDrawConstants.Release();
        m_impl->shadowAttribsCB.Release();
        m_impl->tonemapSRB.Release();
        m_impl->tonemapPSO.Release();
        m_impl->postConstants.Release();
        m_impl->wireframeSRB.Release();
        m_impl->wireframePSO.Release();
        m_impl->wireframeConstants.Release();
        m_impl->wireframeVB.Release();
        m_impl->skySRB.Release();
        m_impl->skyPSO.Release();
        m_impl->skyConstants.Release();
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

    void Renderer::BeginFrame(const Color &c) {
#ifdef TOON_SHADER_HOT_RELOAD
        // Roadmap #10: one atomic-bool check, every frame; real work (the hash comparison
        // Reload() does per tracked file) only runs on the rare frame right after efsw's
        // watch thread actually saw a .hlsl/.hlsli file change.
        if (m_impl->shadersDirty.exchange(false, std::memory_order_relaxed)) {
            const Uint32 n = m_impl->stateCache->Reload();
            std::fprintf(stderr, "[ShaderHotReload] Reloaded %u state(s)\n", n);
        }
#endif

        // The scene renders into three offscreen targets (consumed in EndScene): HDR
        // color, a world-space normal G-buffer (SSAO), and NDC motion vectors (SSAO
        // temporal / DoF), sharing the scene depth buffer.
        ITextureView *rtvs[] = {m_impl->hdrColor->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET),
                                m_impl->normalBuffer->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET),
                                m_impl->motionVectors->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET)};
        ITextureView *dsv = m_impl->sceneDepth->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL);
        m_impl->context->SetRenderTargets(3, rtvs, dsv, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        const float clear[] = {c.r, c.g, c.b, c.a};
        const float zero[] = {0.0f, 0.0f, 0.0f, 0.0f}; // background: zero normal + zero motion
        m_impl->context->ClearRenderTarget(rtvs[0], clear, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        m_impl->context->ClearRenderTarget(rtvs[1], zero, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        m_impl->context->ClearRenderTarget(rtvs[2], zero, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        m_impl->context->ClearDepthStencil(dsv, CLEAR_DEPTH_FLAG, 1.0f, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    }

    uint32_t Renderer::ReloadShaders() {
#ifdef TOON_SHADER_HOT_RELOAD
        return m_impl->stateCache->Reload();
#else
        return 0; // hot-reload is compiled out entirely in a Release build -- a real no-op
#endif
    }

    void Renderer::SetPostParams(const PostParams &params) { m_impl->post = params; }

    void Renderer::EndScene() {
        // Run the enabled post effects first (they bind their own render targets, so
        // this must precede binding the back buffer below). RunPostFX yields:
        //  - colorSRV: the scene after DoF + Bloom (a drop-in HDR resolve input, so
        //    tonemap.hlsl is unchanged), or null -> resolve the raw scene.
        //  - aoSRV: SSAO visibility, or null -> the white "no occlusion" default.
        //  - ssrSRV: SSR reflection radiance, or null -> the black "no reflection" default.
        ITextureView *colorSRV = nullptr;
        ITextureView *aoSRV = nullptr;
        ITextureView *ssrSRV = nullptr;
        m_impl->RunPostFX(m_impl->post, colorSRV, aoSRV, ssrSRV);

        // Resolve inputs: processed color or raw scene; real AO/SSR or their 1x1 defaults.
        ITextureView *postInput = colorSRV ? colorSRV : m_impl->hdrColor->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
        ITextureView *aoInput = aoSRV ? aoSRV : m_impl->aoWhite->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
        ITextureView *ssrInput = ssrSRV ? ssrSRV : m_impl->ssrBlack->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);

        // Resolve the HDR source to the back buffer: SSAO darkening + exposure + tone
        // map. Leaves the back-buffer RTV bound so the UI overlay draws on top.
        ITextureView *rtv = m_impl->swapChain->GetCurrentBackBufferRTV();
        m_impl->context->SetRenderTargets(1, &rtv, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        {
            MapHelper<PostConstants> cb(m_impl->context, m_impl->postConstants, MAP_WRITE, MAP_FLAG_DISCARD);
            cb->exposure = m_impl->post.exposure;
            cb->toneMap = m_impl->post.toneMap ? 1.0f : 0.0f;
            cb->outputSRGB = m_impl->outputSRGB ? 1.0f : 0.0f;
            // Only apply strength when the matching effect produced a real texture (else
            // the default is bound and the composite is a no-op anyway).
            cb->ssaoStrength = aoSRV ? m_impl->post.ssaoStrength : 0.0f;
            cb->ssrStrength = ssrSRV ? m_impl->post.ssrStrength : 0.0f;
            cb->pad0 = cb->pad1 = cb->pad2 = 0.0f;
        }

        // Point the resolve at this frame's HDR source + AO + SSR. All dynamic, so they
        // may safely differ from last frame (scene <-> bloom, effects on/off, or a resize).
        if (auto *v = m_impl->tonemapSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_HDRColor")) { v->Set(postInput); }
        if (auto *v = m_impl->tonemapSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_AO")) { v->Set(aoInput); }
        if (auto *v = m_impl->tonemapSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_SSR")) { v->Set(ssrInput); }

        m_impl->context->SetPipelineState(m_impl->tonemapPSO);
        m_impl->context->CommitShaderResources(m_impl->tonemapSRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        DrawAttribs draw;
        draw.NumVertices = 3; // full-screen triangle
        draw.Flags = DRAW_FLAG_VERIFY_ALL;
        m_impl->context->Draw(draw);

        // Snapshot this frame's now-finalized depth into prevSceneDepth, ready to be the
        // REAL "previous frame" for next frame's RunPostFX (see its declaration + the
        // pPrevDepthBufferSRV comment in RunPostFX). Must happen after RunPostFX above --
        // that call still needs the OLD prevSceneDepth (last frame's) as "previous" for
        // THIS frame's reprojection.
        CopyTextureAttribs copyDepth;
        copyDepth.pSrcTexture = m_impl->sceneDepth;
        copyDepth.SrcTextureTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
        copyDepth.pDstTexture = m_impl->prevSceneDepth;
        copyDepth.DstTextureTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
        m_impl->context->CopyTexture(copyDepth);
    }

    void Renderer::EndFrame() {
        m_impl->swapChain->Present(); // vsync on by default
    }

    void Renderer::Resize(uint32_t width, uint32_t height) {
        if (!m_impl->swapChain || width == 0 || height == 0) { return; }
        m_impl->swapChain->Resize(width, height);
        const SwapChainDesc &sc = m_impl->swapChain->GetDesc();
        CreateOffscreenTargets(sc.Width, sc.Height); // match the new back-buffer size
        // The resolve's HDR input (g_HDRColor) is dynamic: EndScene re-points it at the
        // recreated target next frame, so there's nothing to rebind here.
    }

    // --- Scene: meshes, camera, lighting, draw ----------------------------------

    MeshHandle Renderer::CreateMesh(const Vertex *vertices, uint32_t vertexCount, const uint32_t *indices,
                                    uint32_t indexCount) {
        if (!vertices || vertexCount == 0 || !indices || indexCount == 0) { return MeshHandle::Invalid; }

        Impl::GpuMesh mesh;
        mesh.indexCount = indexCount;
        mesh.boundsMin = vertices[0].position;
        mesh.boundsMax = vertices[0].position;
        for (uint32_t i = 1; i < vertexCount; ++i) {
            // Plain comparisons, not std::min/max: dwmapi.h (above) pulls in <windows.h>
            // without NOMINMAX, so min/max are reserved macros in this TU.
            const Vec3 &p = vertices[i].position;
            if (p.x < mesh.boundsMin.x) { mesh.boundsMin.x = p.x; }
            if (p.y < mesh.boundsMin.y) { mesh.boundsMin.y = p.y; }
            if (p.z < mesh.boundsMin.z) { mesh.boundsMin.z = p.z; }
            if (p.x > mesh.boundsMax.x) { mesh.boundsMax.x = p.x; }
            if (p.y > mesh.boundsMax.y) { mesh.boundsMax.y = p.y; }
            if (p.z > mesh.boundsMax.z) { mesh.boundsMax.z = p.z; }
        }

        BufferDesc vbDesc;
        vbDesc.Name = "toon mesh VB";
        vbDesc.Usage = USAGE_IMMUTABLE;
        vbDesc.BindFlags = BIND_VERTEX_BUFFER;
        vbDesc.Size = static_cast<Uint64>(vertexCount) * sizeof(Vertex);
        BufferData vbData{vertices, vbDesc.Size};
        m_impl->device->CreateBuffer(vbDesc, &vbData, &mesh.vertexBuffer);

        BufferDesc ibDesc;
        ibDesc.Name = "toon mesh IB";
        ibDesc.Usage = USAGE_IMMUTABLE;
        ibDesc.BindFlags = BIND_INDEX_BUFFER;
        ibDesc.Size = static_cast<Uint64>(indexCount) * sizeof(uint32_t);
        BufferData ibData{indices, ibDesc.Size};
        m_impl->device->CreateBuffer(ibDesc, &ibData, &mesh.indexBuffer);

        if (!mesh.vertexBuffer || !mesh.indexBuffer) { return MeshHandle::Invalid; }

        m_impl->meshes.push_back(std::move(mesh));
        return static_cast<MeshHandle>(m_impl->meshes.size()); // 1-based; 0 = Invalid
    }

    void Renderer::SetCamera(const Camera &cam) {
        const SwapChainDesc &sc = m_impl->swapChain->GetDesc();
        const float aspect = sc.Height > 0 ? static_cast<float>(sc.Width) / static_cast<float>(sc.Height) : 1.0f;

        // Orbit the pivot: translate the world so the pivot sits at the origin, then the
        // turntable (rotate about the origin, push `distance` down +Z in front of the
        // left-handed camera at the origin). Pan/fly move the pivot; zoom changes distance.
        const float4x4 view = float4x4::Translation(-cam.pivot.x, -cam.pivot.y, -cam.pivot.z) *
                              float4x4::RotationY(cam.yaw) * float4x4::RotationX(cam.pitch) *
                              float4x4::Translation(0.0f, 0.0f, cam.distance);
        // NegativeOneToOneZ = false -> [0,1] depth range for Vulkan/D3D. 2D editor mode
        // (roadmap #14) swaps in an orthographic matrix: orthoHeight names the world-space
        // vertical extent directly (there's no field-of-view equivalent once nothing shrinks
        // with distance), width follows from the same aspect ratio the perspective branch uses.
        float4x4 proj = cam.orthographic
            ? float4x4::Ortho(cam.orthoHeight * aspect, cam.orthoHeight, cam.nearZ, cam.farZ, false)
            : float4x4::Projection(cam.fovY, aspect, cam.nearZ, cam.farZ, false);
        m_impl->orthographic = cam.orthographic; // DrawGrid reads this to pick its plane flags

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
        m_impl->view = view;
        m_impl->proj = proj;
        m_impl->viewProj = view * proj;
        m_impl->nearZ = cam.nearZ;
        m_impl->farZ = cam.farZ;
    }

    void Renderer::SetLight(const Vec3 &directionToLight, const Vec3 &color, float intensity) {
        m_impl->lightDir = float3(directionToLight.x, directionToLight.y, directionToLight.z);
        m_impl->lightColor = float3(color.x, color.y, color.z) * intensity;
    }

    // Object -> world (Diligent is row-major / row-vector: v * M). `t.rotation` is a plain
    // toon::Quat (core/math.h); converting to Diligent's QuaternionF here (not before) is
    // what lets Transform stay Diligent-free while this, the one file allowed to touch
    // Diligent, still builds on its (numerically fiddlier) quaternion-to-matrix math
    // instead of reimplementing it. Must match scene.cpp's LocalFromTransform exactly;
    // the scene graph and this single-object path compose the same way.
    static float4x4 WorldFromTransform(const Transform &t) {
        const QuaternionF q(t.rotation.x, t.rotation.y, t.rotation.z, t.rotation.w);
        return float4x4::Scale(t.scale.x, t.scale.y, t.scale.z) * q.ToMatrix() *
               float4x4::Translation(t.position.x, t.position.y, t.position.z);
    }

    // Plain Mat4 (seam vocabulary) <-> Diligent float4x4: both row-major, so a straight
    // element copy. The scene graph composes world matrices on the Diligent side and hands
    // them across the seam as Mat4.
    static Mat4 ToMat4(const float4x4 &m) {
        Mat4 out;
        for (int r = 0; r < 4; ++r) {
            for (int c = 0; c < 4; ++c) {
                out.m[r * 4 + c] = m[r][c];
            }
        }
        return out;
    }
    static float4x4 ToFloat4x4(const Mat4 &in) {
        float4x4 out;
        for (int r = 0; r < 4; ++r) {
            for (int c = 0; c < 4; ++c) {
                out[r][c] = in.m[r * 4 + c];
            }
        }
        return out;
    }

    // Row-vector transform (v' = v * M), matching the HLSL shaders' own convention
    // (toon_common.hlsli: "vectors are transformed row-vector style: mul(v, M)"). Diligent's
    // free `operator*(Matrix4x4, Vector4)` is the opposite (column-vector, M * v), so
    // ScreenPointToRay's unproject needs this instead.
    static float4 TransformRowVector(const float4 &v, const float4x4 &m) {
        return float4(v.x * m[0][0] + v.y * m[1][0] + v.z * m[2][0] + v.w * m[3][0],
                      v.x * m[0][1] + v.y * m[1][1] + v.z * m[2][1] + v.w * m[3][1],
                      v.x * m[0][2] + v.y * m[1][2] + v.z * m[2][2] + v.w * m[3][2],
                      v.x * m[0][3] + v.y * m[1][3] + v.z * m[2][3] + v.w * m[3][3]);
    }

    // Debug wireframe overlay (M2.1's collider visualization) -- see core/renderer.h's
    // DrawWireframe comment for the call-timing contract (after EndScene, before BeginUI).
    void Renderer::DrawWireframe(const Mat4 &world, const Vec3 *points, uint32_t count, const Color &color) {
        if (!points || count == 0 || !m_impl->wireframePSO) { return; }
        if (count > kMaxWireframeVertices) {
            std::fprintf(stderr, "DrawWireframe: %u points exceeds the %u max, clamping\n", count,
                         kMaxWireframeVertices);
            count = kMaxWireframeVertices;
        }

        const float4x4 wvp = ToFloat4x4(world) * m_impl->viewProj;
        {
            MapHelper<WireframeConstants> cb(m_impl->context, m_impl->wireframeConstants, MAP_WRITE, MAP_FLAG_DISCARD);
            cb->worldViewProj = wvp;
            cb->color = float4(color.r, color.g, color.b, color.a);
        }
        {
            MapHelper<float3> vb(m_impl->context, m_impl->wireframeVB, MAP_WRITE, MAP_FLAG_DISCARD);
            float3 *dst = vb;
            for (uint32_t i = 0; i < count; ++i) { dst[i] = float3(points[i].x, points[i].y, points[i].z); }
        }

        IBuffer *vbs[] = {m_impl->wireframeVB};
        const Uint64 offsets[] = {0};
        m_impl->context->SetVertexBuffers(0, 1, vbs, offsets, RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                                          SET_VERTEX_BUFFERS_FLAG_RESET);

        m_impl->context->SetPipelineState(m_impl->wireframePSO);
        m_impl->context->CommitShaderResources(m_impl->wireframeSRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        DrawAttribs draw;
        draw.NumVertices = count;
        draw.Flags = DRAW_FLAG_VERIFY_ALL;
        m_impl->context->Draw(draw);
    }

    // --- Editor backdrop: sky gradient + ground grid (roadmap #12) --------------

    // Fullscreen gradient sky, lerped by the world-space view ray's Y direction (not screen
    // Y), so the horizon stays level as the camera pitches -- ToonEngineOld's grid.frag sky,
    // minus the grid (see DrawGrid for that half). Draws into the offscreen HDR G-buffer
    // BeginFrame already bound + cleared; depth is off, so this always fully covers the frame
    // and leaves BeginFrame's far-plane depth clear intact for every pixel opaque geometry
    // doesn't later draw over.
    void Renderer::DrawSky(const Color &top, const Color &bottom) {
        if (!m_impl->skyPSO) { return; }

        {
            MapHelper<SkyConstants> cb(m_impl->context, m_impl->skyConstants, MAP_WRITE, MAP_FLAG_DISCARD);
            cb->invViewProj = m_impl->viewProj.Inverse();
            cb->skyTop = float4(top.r, top.g, top.b, top.a);
            cb->skyBottom = float4(bottom.r, bottom.g, bottom.b, bottom.a);
        }

        m_impl->context->SetPipelineState(m_impl->skyPSO);
        m_impl->context->CommitShaderResources(m_impl->skySRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        DrawAttribs draw;
        draw.NumVertices = 3; // full-screen triangle
        draw.Flags = DRAW_FLAG_VERIFY_ALL;
        m_impl->context->Draw(draw);
    }

    // Infinite grid on the XZ (Y=0) ground plane, or the XY sprite plane while 2D editor mode
    // (roadmap #14) is active (see the FeatureFlags branch below), built on DiligentFX's
    // CoordinateGridRenderer (see CreateGridRenderer). Unlike DrawWireframe, it occludes
    // itself by READING the finished scene depth buffer rather than writing its own -- so it
    // must run after
    // EndScene() resolves that depth (same call-timing contract as DrawWireframe) rather than
    // during the main pass, where sceneDepth is still bound as the write target, not readable
    // as a texture.
    //
    // CoordinateGridRenderer::Render binds only the color target it's given and unbinds every
    // render target again before returning (see its own RenderGridAxes), unlike this file's
    // own passes, which leave whatever they bound in place for the next call. Rebind the back
    // buffer afterward so DrawWireframe and the ImGui overlay right after this still have a
    // target to draw onto, preserving EndScene's "leaves the back buffer bound" contract.
    void Renderer::DrawGrid() {
        if (!m_impl->gridRenderer) { return; }

        const SwapChainDesc &sc = m_impl->swapChain->GetDesc();
        // RunPostFX (EndScene) only fills postCamera when at least one post effect is
        // enabled; the grid must be positioned correctly regardless, so fill it here too.
        m_impl->FillCameraAttribs(sc);

        CoordinateGridRenderer::RenderAttributes attribs;
        attribs.pDevice = m_impl->device;
        attribs.pStateCache = m_impl->stateCache;
        attribs.pDeviceContext = m_impl->context;
        attribs.pColorRTV = m_impl->swapChain->GetCurrentBackBufferRTV();
        attribs.pDepthSRV = m_impl->sceneDepth->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
        attribs.pCamera = &m_impl->postCamera;
        // 2D editor mode (roadmap #14): the sprite plane is XY (facing +Z), not the ground's
        // XZ, so the grid switches which plane/axes it draws to match -- the XZ grid would
        // appear edge-on (collapsed to a single line) from a camera locked to face sprites.
        attribs.FeatureFlags = m_impl->orthographic
            ? (CoordinateGridRenderer::FEATURE_FLAG_RENDER_PLANE_XY | CoordinateGridRenderer::FEATURE_FLAG_RENDER_AXIS_X |
               CoordinateGridRenderer::FEATURE_FLAG_RENDER_AXIS_Y)
            : (CoordinateGridRenderer::FEATURE_FLAG_RENDER_PLANE_XZ | CoordinateGridRenderer::FEATURE_FLAG_RENDER_AXIS_X |
               CoordinateGridRenderer::FEATURE_FLAG_RENDER_AXIS_Z);
        if (m_impl->outputSRGB) { attribs.FeatureFlags |= CoordinateGridRenderer::FEATURE_FLAG_CONVERT_TO_SRGB; }
        attribs.pAttribs = &m_impl->gridAttribs;

        m_impl->gridRenderer->Render(attribs);

        ITextureView *rtv = m_impl->swapChain->GetCurrentBackBufferRTV();
        m_impl->context->SetRenderTargets(1, &rtv, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    }

    // --- 2D sprites (roadmap #13) -------------------------------------------------

    // A no-op on an invalid handle (see the header's own comment): lets the app layer skip
    // its own "texture still loading/failed" check and just always call this for every
    // sprite entity. g_SpriteTex is re-Set on the one shared spriteSRB every call, the same
    // per-draw-varying-texture pattern DrawModel uses for g_Albedo (see CreateModelPipeline).
    // `prevWorld` feeds the sprite's own motion vector (see CreateSpritePipeline's blend
    // comment for why the sprite writes real G-buffer data); viewProj/prevViewProj are the
    // same (TAA-jittered, when enabled) matrices every other draw uses, so temporal
    // reprojection sees the sprite move consistently with the rest of the scene.
    void Renderer::DrawSprite(const Mat4 &world, const Mat4 &prevWorld, TextureHandle texture, const Vec4 &tint,
                              const Vec4 &uvRect) {
        const uint32_t idx = static_cast<uint32_t>(texture);
        if (idx == 0 || idx > m_impl->textures.size() || !m_impl->textures[idx - 1] || !m_impl->spritePSO) {
            return;
        }

        {
            MapHelper<SpriteConstants> cb(m_impl->context, m_impl->spriteConstants, MAP_WRITE, MAP_FLAG_DISCARD);
            cb->worldViewProj = ToFloat4x4(world) * m_impl->viewProj;
            cb->prevWorldViewProj = ToFloat4x4(prevWorld) * m_impl->prevViewProj;
            cb->world = ToFloat4x4(world);
            cb->tint = float4(tint.x, tint.y, tint.z, tint.w);
            cb->uvRect = float4(uvRect.x, uvRect.y, uvRect.z, uvRect.w);
        }

        IBuffer *vbs[] = {m_impl->spriteQuadVB};
        const Uint64 offsets[] = {0};
        m_impl->context->SetVertexBuffers(0, 1, vbs, offsets, RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                                          SET_VERTEX_BUFFERS_FLAG_RESET);

        ITextureView *srv = m_impl->textures[idx - 1]->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
        if (auto *v = m_impl->spriteSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_SpriteTex")) { v->Set(srv); }

        m_impl->context->SetPipelineState(m_impl->spritePSO);
        m_impl->context->CommitShaderResources(m_impl->spriteSRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        DrawAttribs draw;
        draw.NumVertices = 6; // the shared unit quad (CreateSpritePipeline), two triangles
        draw.Flags = DRAW_FLAG_VERIFY_ALL;
        m_impl->context->Draw(draw);
    }

    // --- In-game UI overlay (roadmap #17) ----------------------------------------

    // Uploads one screen-space UI batch (tinted quads + MSDF text) and draws it onto the
    // resolved back buffer. Call after EndScene(), the same timing as DrawWireframe/DrawGrid;
    // it binds the back buffer itself (like DrawGrid's rebind) so it's correct regardless of
    // whether the grid/wireframe overlays ran first. See UIVertex for the vertex format and
    // CreateUIPipeline for the pipeline. g_UIAtlas is re-Set per call, the same per-draw-varying-
    // texture pattern DrawSprite uses for g_SpriteTex.
    void Renderer::DrawUI(const UIVertex *vertices, uint32_t vertexCount, TextureHandle atlas, float pixelRange) {
        if (!vertices || vertexCount == 0 || !m_impl->uiPSO) { return; }
        if (vertexCount > kMaxUIVertices) {
            std::fprintf(stderr, "DrawUI: %u verts exceeds the %u max, clamping\n", vertexCount, kMaxUIVertices);
            vertexCount = kMaxUIVertices;
        }

        // The atlas SRV (or the 1x1 white fallback for a solid-only batch) + its texel size,
        // which ui.hlsl's MSDF median needs for its screen-pixel-range anti-aliasing.
        const uint32_t texIdx = static_cast<uint32_t>(atlas);
        ITexture *atlasTex = (texIdx != 0 && texIdx <= m_impl->textures.size() && m_impl->textures[texIdx - 1])
                                 ? m_impl->textures[texIdx - 1].RawPtr()
                                 : m_impl->uiWhiteTex.RawPtr();

        const SwapChainDesc &sc = m_impl->swapChain->GetDesc();
        {
            MapHelper<UIConstants> cb(m_impl->context, m_impl->uiConstants, MAP_WRITE, MAP_FLAG_DISCARD);
            cb->screenSize = float2(static_cast<float>(sc.Width), static_cast<float>(sc.Height));
            cb->atlasSize = float2(static_cast<float>(atlasTex->GetDesc().Width),
                                   static_cast<float>(atlasTex->GetDesc().Height));
            cb->pixelRange = pixelRange;
        }
        {
            MapHelper<UIVertex> vb(m_impl->context, m_impl->uiVB, MAP_WRITE, MAP_FLAG_DISCARD);
            UIVertex *dst = vb;
            for (uint32_t i = 0; i < vertexCount; ++i) { dst[i] = vertices[i]; }
        }

        IBuffer *vbs[] = {m_impl->uiVB};
        const Uint64 offsets[] = {0};
        m_impl->context->SetVertexBuffers(0, 1, vbs, offsets, RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                                          SET_VERTEX_BUFFERS_FLAG_RESET);

        ITextureView *srv = atlasTex->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
        if (auto *v = m_impl->uiSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_UIAtlas")) { v->Set(srv); }
        if (auto *v = m_impl->uiSRB->GetVariableByName(SHADER_TYPE_VERTEX, "g_UIAtlas")) { v->Set(srv); }

        ITextureView *rtv = m_impl->swapChain->GetCurrentBackBufferRTV();
        m_impl->context->SetRenderTargets(1, &rtv, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        m_impl->context->SetPipelineState(m_impl->uiPSO);
        m_impl->context->CommitShaderResources(m_impl->uiSRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        DrawAttribs draw;
        draw.NumVertices = vertexCount;
        draw.Flags = DRAW_FLAG_VERIFY_ALL;
        m_impl->context->Draw(draw);
    }

    // --- Cascaded shadow maps ----------------------------------------------------

    // Distributes cascades from the current camera (SetCamera) + light (SetLight) and
    // uploads ShadowMapAttribs for the main fill PSOs to read. Returns the cascade count to
    // loop over: 0 when PostParams::shadows is off (or no cascades were configured), in
    // which case the shadow map is untouched and the fill shaders skip sampling it entirely
    // -- iNumCascades = 0 is Shadows.fxh's own "no shadows" sentinel (FindCascade's search
    // loop never runs, FilterShadowMap short-circuits to fLightAmount = 1.0), so this is
    // also what correctly blanks a stale/never-rendered shadow map on the first frame or
    // right after the toggle turns shadows off.
    uint32_t Renderer::BeginShadowPass() {
        if (!m_impl->post.shadows) {
            m_impl->shadowMapAttribs.iNumCascades = 0;
            MapHelper<ShadowMapAttribs> cb(m_impl->context, m_impl->shadowAttribsCB, MAP_WRITE, MAP_FLAG_DISCARD);
            *cb = m_impl->shadowMapAttribs;
            return 0;
        }

        ShadowMapManager::DistributeCascadeInfo distInfo;
        distInfo.pCameraView = &m_impl->view;
        distInfo.pCameraProj = &m_impl->proj;
        distInfo.pLightDir = &m_impl->lightDir;
        // Matches the SHADER_COMPILE_FLAG_PACK_MATRIX_ROW_MAJOR set on the fill shaders
        // (see CreateToonShader's comment): row-major throughout, no transpose mismatch.
        distInfo.PackMatrixRowMajor = true;
        m_impl->shadowMap.DistributeCascades(distInfo, m_impl->shadowMapAttribs);

        MapHelper<ShadowMapAttribs> cb(m_impl->context, m_impl->shadowAttribsCB, MAP_WRITE, MAP_FLAG_DISCARD);
        *cb = m_impl->shadowMapAttribs;

        return kShadowCascades;
    }

    // Binds cascade `cascadeIndex`'s depth target and clears it. DrawMeshShadow/
    // DrawModelShadow calls in between render into whichever cascade was bound last.
    void Renderer::BeginShadowCascade(uint32_t cascadeIndex) {
        m_impl->currentShadowCascade = cascadeIndex;
        ITextureView *dsv = m_impl->shadowMap.GetCascadeDSV(cascadeIndex);
        m_impl->context->SetRenderTargets(0, nullptr, dsv, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        m_impl->context->ClearDepthStencil(dsv, CLEAR_DEPTH_FLAG, 1.0f, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    }

    // Depth-only draw into the currently-bound cascade: position transformed straight into
    // that cascade's light-space clip position (no material, no motion vectors -- the
    // shadow map carries no color/history of its own).
    void Renderer::DrawMeshShadow(MeshHandle handle, const Mat4 &worldM) {
        const uint32_t idx = static_cast<uint32_t>(handle);
        if (idx == 0 || idx > m_impl->meshes.size()) { return; }
        const Impl::GpuMesh &mesh = m_impl->meshes[idx - 1];

        const float4x4 world = ToFloat4x4(worldM);
        const float4x4 &lightProj = m_impl->shadowMap.GetCascadeTransform(m_impl->currentShadowCascade).WorldToLightProjSpace;

        {
            MapHelper<float4x4> cb(m_impl->context, m_impl->shadowDrawConstants, MAP_WRITE, MAP_FLAG_DISCARD);
            *cb = world * lightProj;
        }

        IBuffer *vbs[] = {mesh.vertexBuffer};
        const Uint64 offsets[] = {0};
        m_impl->context->SetVertexBuffers(0, 1, vbs, offsets, RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                                          SET_VERTEX_BUFFERS_FLAG_RESET);
        m_impl->context->SetIndexBuffer(mesh.indexBuffer, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        DrawIndexedAttribs draw;
        draw.IndexType = VT_UINT32;
        draw.NumIndices = mesh.indexCount;
        draw.Flags = DRAW_FLAG_VERIFY_ALL;

        m_impl->context->SetPipelineState(m_impl->shadowPSO);
        m_impl->context->CommitShaderResources(m_impl->shadowSRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        m_impl->context->DrawIndexed(draw);
    }

    // Same idea as DrawModel, stripped to depth-only: walk the model's nodes, transform each
    // into the currently-bound cascade's light-space clip position, no material/albedo. A
    // null `anim` (the default) draws bind pose through the static PSO, byte-for-byte the
    // pre-roadmap-#11 path; a non-null one samples the given clip/time via ComputeTransforms
    // and routes any node with a skin (GLTF::Node::pSkin != nullptr) through the skinned PSO
    // instead, so an animated character's shadow follows its animated silhouette. No motion
    // vectors in a depth-only pass, so only the current pose is sampled (unlike DrawModel,
    // which also needs the previous one).
    void Renderer::DrawModelShadow(ModelHandle handle, const Mat4 &worldM, const AnimationState *anim) {
        const uint32_t idx = static_cast<uint32_t>(handle);
        if (idx == 0 || idx > m_impl->models.size()) { return; }
        GLTF::Model &model = *m_impl->models[idx - 1];
        if (model.Scenes.empty()) { return; }
        const int sceneId = model.DefaultSceneId;

        const float4x4 objWorld = ToFloat4x4(worldM);
        const float4x4 &lightProj = m_impl->shadowMap.GetCascadeTransform(m_impl->currentShadowCascade).WorldToLightProjSpace;

        GLTF::ModelTransforms xforms;
        if (anim != nullptr) {
            model.ComputeTransforms(sceneId, xforms, float4x4::Identity(), anim->clipIndex, anim->time);
        } else {
            model.ComputeTransforms(sceneId, xforms);
        }

        IBuffer *vbs[8] = {};
        const Uint32 numVBs = static_cast<Uint32>(model.GetVertexBufferCount());
        for (Uint32 i = 0; i < numVBs; ++i) {
            vbs[i] = model.GetVertexBuffer(i, m_impl->device, m_impl->context);
        }
        m_impl->context->SetVertexBuffers(0, numVBs, vbs, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                                          SET_VERTEX_BUFFERS_FLAG_RESET);
        IBuffer *ib = model.GetIndexBuffer(m_impl->device, m_impl->context);
        if (ib) { m_impl->context->SetIndexBuffer(ib, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION); }

        const Uint32 baseIndex = model.GetFirstIndexLocation();
        const Uint32 baseVertex = model.GetBaseVertex();

        const GLTF::Scene &scene = model.Scenes[sceneId];
        for (const GLTF::Node *node : scene.LinearNodes) {
            if (node->pMesh == nullptr) { continue; }
            const float4x4 world = xforms.NodeGlobalMatrices[node->Index] * objWorld;

            // Chosen per node, not per model: a model mixing skinned and unskinned meshes
            // still draws each node through the PSO that matches its own vertex data.
            const bool skinned = node->pSkin != nullptr;
            if (skinned) {
                const auto &joints = xforms.Skins[node->SkinTransformsIndex].JointMatrices;
                const Uint32 jointCount = static_cast<Uint32>(joints.size());
                EnsureJointsBufferCapacity(jointCount);
                MapHelper<float4x4> jb(m_impl->context, m_impl->jointsBuffer, MAP_WRITE, MAP_FLAG_DISCARD);
                std::memcpy(static_cast<float4x4 *>(jb), joints.data(), jointCount * sizeof(float4x4));
            }

            {
                MapHelper<float4x4> cb(m_impl->context, m_impl->shadowDrawConstants, MAP_WRITE, MAP_FLAG_DISCARD);
                *cb = world * lightProj;
            }
            // Re-commit after every remap: a fresh Map/Discard may hand back a different
            // underlying GPU allocation, which the SRB needs to be told about again before
            // the next draw -- same idiom DrawMesh/DrawModel already use per primitive.
            IPipelineState *pso = skinned ? m_impl->modelShadowSkinnedPSO : m_impl->modelShadowPSO;
            IShaderResourceBinding *srb = skinned ? m_impl->modelShadowSkinnedSRB : m_impl->modelShadowSRB;
            m_impl->context->SetPipelineState(pso);
            m_impl->context->CommitShaderResources(srb, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

            for (const GLTF::Primitive &prim : node->pMesh->Primitives) {
                if (prim.VertexCount == 0 && prim.IndexCount == 0) { continue; }
                if (prim.IndexCount > 0) {
                    DrawIndexedAttribs draw;
                    draw.IndexType = VT_UINT32;
                    draw.NumIndices = prim.IndexCount;
                    draw.FirstIndexLocation = baseIndex + prim.FirstIndex;
                    draw.BaseVertex = baseVertex + prim.FirstVertex;
                    draw.Flags = DRAW_FLAG_VERIFY_ALL;
                    m_impl->context->DrawIndexed(draw);
                } else {
                    DrawAttribs draw;
                    draw.NumVertices = prim.VertexCount;
                    draw.StartVertexLocation = baseVertex + prim.FirstVertex;
                    draw.Flags = DRAW_FLAG_VERIFY_ALL;
                    m_impl->context->Draw(draw);
                }
            }
        }
    }

    // No-op today (PCF needs no post-pass); present for symmetry with BeginShadowPass and as
    // the one seam entry point future filtering modes (VSM/EVSM's ConvertToFilterable) would
    // hook into without changing main.cpp's call shape.
    void Renderer::EndShadowPass() {}

    // Expose the current view + projection (as of the last SetCamera) for the editor's transform
    // gizmo. ImGuizmo (in main.cpp) needs them to project the gizmo onto the selected entity; the
    // seam hands them out as plain Mat4 so ImGuizmo stays Diligent-free. (The proj may carry the
    // sub-pixel TAA jitter, negligible for a UI overlay, and TAA is off by default.)
    void Renderer::GetViewProj(Mat4 &view, Mat4 &proj) const {
        view = ToMat4(m_impl->view);
        proj = ToMat4(m_impl->proj);
    }

    // Mouse-pick's unproject (app/picking.cpp): pixel -> NDC -> world, via the inverse of the
    // SAME view*proj SetCamera built this frame (m_impl->viewProj), so the ray always matches
    // what's on screen.
    void Renderer::ScreenPointToRay(float mouseX, float mouseY, float vpW, float vpH, Vec3 &outOrigin,
                                    Vec3 &outDir) const {
        if (vpW <= 0.0f || vpH <= 0.0f) {
            outOrigin = {};
            outDir = {0.0f, 0.0f, 1.0f};
            return;
        }

        // Pixel -> NDC; Y flips (pixel Y grows down, NDC Y grows up).
        const float ndcX = (2.0f * mouseX / vpW) - 1.0f;
        const float ndcY = 1.0f - (2.0f * mouseY / vpH);

        // Diligent/Vulkan depth range is [0,1] (NOT OpenGL's [-1,1]) -- near = z 0, far = z 1.
        const float4x4 invViewProj = m_impl->viewProj.Inverse();
        float4 nearWorld = TransformRowVector(float4(ndcX, ndcY, 0.0f, 1.0f), invViewProj);
        float4 farWorld = TransformRowVector(float4(ndcX, ndcY, 1.0f, 1.0f), invViewProj);
        nearWorld /= nearWorld.w;
        farWorld /= farWorld.w;

        outOrigin = {nearWorld.x, nearWorld.y, nearWorld.z};
        outDir = Normalize(Vec3{farWorld.x - nearWorld.x, farWorld.y - nearWorld.y, farWorld.z - nearWorld.z});
    }

    bool Renderer::GetMeshBounds(MeshHandle mesh, Vec3 &outMin, Vec3 &outMax) const {
        const uint32_t idx = static_cast<uint32_t>(mesh);
        if (idx == 0 || idx > m_impl->meshes.size()) { return false; }
        outMin = m_impl->meshes[idx - 1].boundsMin;
        outMax = m_impl->meshes[idx - 1].boundsMax;
        return true;
    }

    bool Renderer::GetModelBounds(ModelHandle model, Vec3 &outMin, Vec3 &outMax) const {
        const uint32_t idx = static_cast<uint32_t>(model);
        if (idx == 0 || idx > m_impl->modelBounds.size()) { return false; }
        outMin = m_impl->modelBounds[idx - 1].first;
        outMax = m_impl->modelBounds[idx - 1].second;
        return true;
    }

    // The toon draw, given a pre-composed object->world matrix (+ last frame's, for motion
    // vectors). The scene graph passes hierarchy-composed world matrices straight in; the
    // Transform overload below builds them from a single object's placement.
    void Renderer::DrawMesh(MeshHandle handle, const Mat4 &worldM, const Mat4 &prevWorldM, const Material &mat) {
        const uint32_t idx = static_cast<uint32_t>(handle);
        if (idx == 0 || idx > m_impl->meshes.size()) { return; }
        const Impl::GpuMesh &mesh = m_impl->meshes[idx - 1];

        const float4x4 world = ToFloat4x4(worldM);
        const float4x4 prevWorld = ToFloat4x4(prevWorldM);
        const float4x4 wvp = world * m_impl->viewProj;
        const float4x4 prevWvp = prevWorld * m_impl->prevViewProj;

        // Normal matrix = inverse-transpose of the world matrix (correct normals under
        // non-uniform scale; its 3x3 transpose is world^-1, used by the outline VS). The
        // previous-frame version is what the outline VS needs to extrude *last* frame's
        // shell for its own motion vector -- see g_PrevNormalMatrix in toon_common.hlsli.
        const float4x4 normalMat = world.Inverse().Transpose();
        const float4x4 prevNormalMat = prevWorld.Inverse().Transpose();

        {
            const float3 &L = m_impl->lightDir;
            const float3 &LC = m_impl->lightColor;
            MapHelper<ShaderConstants> cb(m_impl->context, m_impl->constants, MAP_WRITE, MAP_FLAG_DISCARD);
            cb->worldViewProj = wvp;
            cb->world = world;
            cb->normalMatrix = normalMat;
            cb->prevWorldViewProj = prevWvp;
            cb->prevNormalMatrix = prevNormalMat;
            cb->lightDir = float4(L.x, L.y, L.z, 0.0f);
            cb->lightColor = float4(LC.x, LC.y, LC.z, 0.0f);
            cb->baseColor = float4(mat.baseColor.x, mat.baseColor.y, mat.baseColor.z, 1.0f);
            cb->outline = float4(mat.outlineColor.x, mat.outlineColor.y, mat.outlineColor.z, mat.outlineWidth);
            cb->params = float4(mat.bands, mat.ambient, mat.roughness, 0.0f);
        }

        IBuffer *vbs[] = {mesh.vertexBuffer};
        const Uint64 offsets[] = {0};
        m_impl->context->SetVertexBuffers(0, 1, vbs, offsets, RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                                          SET_VERTEX_BUFFERS_FLAG_RESET);
        m_impl->context->SetIndexBuffer(mesh.indexBuffer, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        DrawIndexedAttribs draw;
        draw.IndexType = VT_UINT32;
        draw.NumIndices = mesh.indexCount;
        draw.Flags = DRAW_FLAG_VERIFY_ALL;

        // Outline first (enlarged back-facing shell), then the fill on top: the
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
    void Renderer::DrawMesh(MeshHandle handle, const Transform &t, const Transform &prevT, const Material &mat) {
        DrawMesh(handle, ToMat4(WorldFromTransform(t)), ToMat4(WorldFromTransform(prevT)), mat);
    }

    // --- Scene: glTF models -----------------------------------------------------

    ModelHandle Renderer::LoadModel(const char *path) {
        if (!path) { return ModelHandle::Invalid; }

        size_t attribCount = 0;
        const GLTF::VertexAttributeDesc *attribs = ModelVertexAttribs(attribCount);

        GLTF::ModelCreateInfo ci;
        ci.FileName = path;
        ci.VertexAttributes = attribs;
        ci.NumVertexAttributes = static_cast<Uint32>(attribCount);
        ci.IndexType = VT_UINT32;
        // The loader defaults vertex buffers to BIND_NONE (only the index buffer defaults to
        // BIND_INDEX_BUFFER); without this a VB can't be bound/drawn. Two buffer slots now
        // (see ModelVertexAttribs): 0 for pos/normal/uv, 1 for JOINTS_0/WEIGHTS_0.
        ci.VertBufferBindFlags[0] = BIND_VERTEX_BUFFER;
        ci.VertBufferBindFlags[1] = BIND_VERTEX_BUFFER;

        // The loader parses + uploads GPU buffers/textures in its constructor; it throws on a
        // bad/missing file, so guard the load.
        std::unique_ptr<GLTF::Model> model;
        try {
            model = std::make_unique<GLTF::Model>(m_impl->device, m_impl->context, ci);
        } catch (const std::exception &e) {
            std::fprintf(stderr, "Renderer: failed to load model '%s': %s\n", path, e.what());
            return ModelHandle::Invalid;
        }
        model->PrepareGPUResources(m_impl->device, m_impl->context);

        // Local-space (model-space) bounds for mouse-pick (app/picking.cpp): ComputeTransforms'
        // default RootTransform is identity, so ComputeBoundingBox returns the model's own
        // object-space box, ready for the app layer to transform by an entity's worldMatrix.
        GLTF::ModelTransforms xforms;
        model->ComputeTransforms(model->DefaultSceneId, xforms);
        const BoundBox bb = model->ComputeBoundingBox(model->DefaultSceneId, xforms);
        m_impl->modelBounds.emplace_back(Vec3{bb.Min.x, bb.Min.y, bb.Min.z}, Vec3{bb.Max.x, bb.Max.y, bb.Max.z});

        m_impl->models.push_back(std::move(model));
        return static_cast<ModelHandle>(m_impl->models.size()); // 1-based; 0 = Invalid
    }

    // Roadmap #11 (skeletal animation): whether the file LoadModel loaded actually rigged
    // anything. A direct read of GLTF::Model::Skins (already kept alive in m_impl->models),
    // not a cached flag -- one vector-empty check per call is cheap enough not to bother.
    bool Renderer::ModelHasSkin(ModelHandle handle) const {
        const uint32_t idx = static_cast<uint32_t>(handle);
        if (idx == 0 || idx > m_impl->models.size()) { return false; }
        return !m_impl->models[idx - 1]->Skins.empty();
    }

    uint32_t Renderer::GetModelAnimationCount(ModelHandle handle) const {
        const uint32_t idx = static_cast<uint32_t>(handle);
        if (idx == 0 || idx > m_impl->models.size()) { return 0; }
        return static_cast<uint32_t>(m_impl->models[idx - 1]->Animations.size());
    }

    std::string Renderer::GetModelAnimationName(ModelHandle handle, uint32_t index) const {
        const uint32_t idx = static_cast<uint32_t>(handle);
        if (idx == 0 || idx > m_impl->models.size()) { return {}; }
        const auto &animations = m_impl->models[idx - 1]->Animations;
        if (index >= animations.size()) { return {}; }
        return animations[index].Name;
    }

    // Animations[i].Start/End are the clip's own playable range in seconds (glTF sampler
    // input times -- not necessarily zero-based, though in practice they always are); the
    // duration is their difference, not End alone.
    float Renderer::GetModelAnimationDuration(ModelHandle handle, uint32_t index) const {
        const uint32_t idx = static_cast<uint32_t>(handle);
        if (idx == 0 || idx > m_impl->models.size()) { return 0.0f; }
        const auto &animations = m_impl->models[idx - 1]->Animations;
        if (index >= animations.size()) { return 0.0f; }
        return animations[index].End - animations[index].Start;
    }

    // Walks every mesh-bearing node in the model's default scene, composing each node's
    // local transform under the object's world matrix, and draws its primitives
    // outline-then-fill (same two-pass order as DrawMesh) with the shared style and each
    // primitive's own albedo texture. A null `anim` (the default) draws bind pose through the
    // static PSOs, byte-for-byte the pre-roadmap-#11 path; a non-null one samples the given
    // clip/time and routes any node with a skin through the skinned PSOs instead (see
    // Impl::modelSkinnedPSO's own comment for why they're separate PSOs, not a shader branch).
    void Renderer::DrawModel(ModelHandle handle, const Mat4 &worldM, const Mat4 &prevWorldM, const Material &style,
                             const AnimationState *anim) {
        const uint32_t idx = static_cast<uint32_t>(handle);
        if (idx == 0 || idx > m_impl->models.size()) { return; }
        GLTF::Model &model = *m_impl->models[idx - 1];
        if (model.Scenes.empty()) { return; }
        const int sceneId = model.DefaultSceneId;

        // Object placement this frame + last (for motion vectors), composed with each node's
        // transform inside the model.
        const float4x4 objWorld = ToFloat4x4(worldM);
        const float4x4 objPrevWorld = ToFloat4x4(prevWorldM);

        // Roadmap #11 (skeletal animation): a non-null `anim` samples the given clip/time (and,
        // separately, last frame's time) via ComputeTransforms -- the same function this call
        // already made for the bind-pose-only path, just fed a real animation index/time
        // instead of the -1/0 defaults. Two full calls, not one: motion vectors on an animated
        // node need its PREVIOUS pose, not just this frame's, and ComputeTransforms has no
        // incremental mode -- it always resolves the whole scene from scratch.
        GLTF::ModelTransforms xforms;
        GLTF::ModelTransforms prevXforms;
        if (anim != nullptr) {
            model.ComputeTransforms(sceneId, xforms, float4x4::Identity(), anim->clipIndex, anim->time);
            model.ComputeTransforms(sceneId, prevXforms, float4x4::Identity(), anim->clipIndex, anim->prevTime);
        } else {
            model.ComputeTransforms(sceneId, xforms);
        }

        // Bind the model's shared vertex + index buffers once; every primitive sub-ranges them.
        IBuffer *vbs[8] = {};
        const Uint32 numVBs = static_cast<Uint32>(model.GetVertexBufferCount());
        for (Uint32 i = 0; i < numVBs; ++i) {
            vbs[i] = model.GetVertexBuffer(i, m_impl->device, m_impl->context);
        }
        m_impl->context->SetVertexBuffers(0, numVBs, vbs, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                                          SET_VERTEX_BUFFERS_FLAG_RESET);
        IBuffer *ib = model.GetIndexBuffer(m_impl->device, m_impl->context);
        if (ib) { m_impl->context->SetIndexBuffer(ib, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION); }

        const Uint32 baseIndex = model.GetFirstIndexLocation();
        const Uint32 baseVertex = model.GetBaseVertex();

        // One primitive's indexed (or non-indexed) draw with the loader's global offsets;
        // issued once per pass (outline, then fill) after that pass's PSO + SRB are bound.
        auto issueDraw = [&](const GLTF::Primitive &p) {
            if (p.IndexCount > 0) {
                DrawIndexedAttribs draw;
                draw.IndexType = VT_UINT32;
                draw.NumIndices = p.IndexCount;
                draw.FirstIndexLocation = baseIndex + p.FirstIndex;
                draw.BaseVertex = baseVertex + p.FirstVertex;
                draw.Flags = DRAW_FLAG_VERIFY_ALL;
                m_impl->context->DrawIndexed(draw);
            } else {
                DrawAttribs draw;
                draw.NumVertices = p.VertexCount;
                draw.StartVertexLocation = baseVertex + p.FirstVertex;
                draw.Flags = DRAW_FLAG_VERIFY_ALL;
                m_impl->context->Draw(draw);
            }
        };

        ITextureView *whiteSRV = m_impl->modelWhite->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
        const float3 &L = m_impl->lightDir;
        const float3 &LC = m_impl->lightColor;
        const GLTF::Scene &scene = model.Scenes[sceneId];

        for (const GLTF::Node *node : scene.LinearNodes) {
            if (node->pMesh == nullptr) { continue; }
            const float4x4 nodeGlobal = xforms.NodeGlobalMatrices[node->Index];
            const float4x4 world = nodeGlobal * objWorld;
            // A static (anim == nullptr) node's local transform is constant, so its only
            // motion comes from the object's own world matrix (the original behavior, reusing
            // nodeGlobal for both). An animated node's local transform changes every frame
            // too -- prevXforms is what actually captures that; reusing nodeGlobal for it
            // would under-report an animated character's motion the same way reusing a
            // single frame's normal matrix would (see g_PrevNormalMatrix's own reasoning).
            const float4x4 prevNodeGlobal = (anim != nullptr) ? prevXforms.NodeGlobalMatrices[node->Index] : nodeGlobal;
            const float4x4 prevWorld = prevNodeGlobal * objPrevWorld;
            const float4x4 normalMat = world.Inverse().Transpose();
            const float4x4 prevNormalMat = prevWorld.Inverse().Transpose(); // see g_PrevNormalMatrix

            // Chosen per node, not per model: a model mixing skinned and unskinned meshes
            // still draws each node through the PSO that matches its own vertex data.
            const bool skinned = node->pSkin != nullptr;
            if (skinned) {
                const auto &currJoints = xforms.Skins[node->SkinTransformsIndex].JointMatrices;
                const auto &prevJoints =
                    (anim != nullptr) ? prevXforms.Skins[node->SkinTransformsIndex].JointMatrices : currJoints;
                const Uint32 jointCount = static_cast<Uint32>(currJoints.size());
                EnsureJointsBufferCapacity(2 * jointCount); // current + previous, back to back
                {
                    MapHelper<float4x4> jb(m_impl->context, m_impl->jointsBuffer, MAP_WRITE, MAP_FLAG_DISCARD);
                    float4x4 *dst = static_cast<float4x4 *>(jb);
                    std::memcpy(dst, currJoints.data(), jointCount * sizeof(float4x4));
                    std::memcpy(dst + jointCount, prevJoints.data(), jointCount * sizeof(float4x4));
                }
                MapHelper<SkinConstants> sc(m_impl->context, m_impl->skinConstants, MAP_WRITE, MAP_FLAG_DISCARD);
                sc->jointCount = jointCount;
            }

            for (const GLTF::Primitive &prim : node->pMesh->Primitives) {
                if (prim.VertexCount == 0 && prim.IndexCount == 0) { continue; }
                const GLTF::Material &mat = model.Materials[prim.MaterialId];
                const float4 bc = mat.Attribs.BaseColorFactor;

                {
                    MapHelper<ShaderConstants> cb(m_impl->context, m_impl->constants, MAP_WRITE, MAP_FLAG_DISCARD);
                    cb->worldViewProj = world * m_impl->viewProj;
                    cb->world = world;
                    cb->normalMatrix = normalMat;
                    cb->prevWorldViewProj = prevWorld * m_impl->prevViewProj;
                    cb->prevNormalMatrix = prevNormalMat;
                    cb->lightDir = float4(L.x, L.y, L.z, 0.0f);
                    cb->lightColor = float4(LC.x, LC.y, LC.z, 0.0f);
                    cb->baseColor = float4(bc.x * style.baseColor.x, bc.y * style.baseColor.y, bc.z * style.baseColor.z,
                                           bc.w); // glTF factor * app tint
                    cb->outline = float4(style.outlineColor.x, style.outlineColor.y, style.outlineColor.z,
                                         style.outlineWidth); // outline pass
                    cb->params = float4(style.bands, style.ambient, style.roughness, 0.0f);
                }

                // Fill-pass albedo: the material's base-color texture, or the 1x1 white
                // 2D-array fallback when it has none.
                ITextureView *albedoSRV = whiteSRV;
                const int tid = mat.GetTextureId(GLTF::DefaultBaseColorTextureAttribId);
                if (tid >= 0) {
                    if (ITexture *tex = model.GetTexture(static_cast<Uint32>(tid), m_impl->device, m_impl->context)) {
                        albedoSRV = tex->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
                    }
                }

                IPipelineState *outlinePSO = skinned ? m_impl->modelOutlineSkinnedPSO : m_impl->modelOutlinePSO;
                IShaderResourceBinding *outlineSRB = skinned ? m_impl->modelOutlineSkinnedSRB : m_impl->modelOutlineSRB;
                IPipelineState *fillPSO = skinned ? m_impl->modelSkinnedPSO : m_impl->modelPSO;
                IShaderResourceBinding *fillSRB = skinned ? m_impl->modelSkinnedSRB : m_impl->modelSRB;

                if (auto *v = fillSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_Albedo")) { v->Set(albedoSRV); }

                // Outline first (enlarged back-facing shell), then the textured fill on top:
                // the fill's nearer depth overwrites the shell everywhere but the silhouette rim.
                m_impl->context->SetPipelineState(outlinePSO);
                m_impl->context->CommitShaderResources(outlineSRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                issueDraw(prim);

                m_impl->context->SetPipelineState(fillPSO);
                m_impl->context->CommitShaderResources(fillSRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                issueDraw(prim);
            }
        }
    }

    // Convenience: a single model instance's placement (no scene parent).
    void Renderer::DrawModel(ModelHandle handle, const Transform &t, const Transform &prevT, const Material &style,
                             const AnimationState *anim) {
        DrawModel(handle, ToMat4(WorldFromTransform(t)), ToMat4(WorldFromTransform(prevT)), style, anim);
    }

    // --- Textures (editor UI: asset thumbnails/previews; also sprite textures) ---

    // Decodes an image file straight to GPU via DiligentTools' loader (PNG/JPG/BMP/TGA).
    // Default TextureLoadInfo is otherwise exactly right here: IMMUTABLE + BIND_SHADER_RESOURCE,
    // mips generated; only IsSRGB is caller-selected (`srgb`, see the header's own comment) --
    // load-bearing in both directions, not just the false case: ImGui's own shader treats a
    // bound texture's samples and its per-vertex colors (authored in gamma space by the editor
    // themes) as the same color space, so an sRGB-sampled thumbnail would linearize on read
    // while the UI around it doesn't, and every thumbnail would come out too dark (srgb=false);
    // a sprite (roadmap #13, DrawSprite) composites into the linear HDR scene like every other
    // draw, so its texture DOES need that linearize-on-sample (srgb=true). CreateTextureFromFile
    // is device-only (uploads mips as immutable initial data, no immediate-context work), so
    // this never touches m_impl->context.
    TextureHandle Renderer::LoadTexture(const char *path, bool srgb) {
        if (!path) { return TextureHandle::Invalid; }

        TextureLoadInfo info;
        info.Name = path;
        info.IsSRGB = srgb; // false (thumbnails): ImGui's own gamma-space shader expects it;
                            // true (sprites): composites into the linear HDR scene, so the
                            // view must linearize on sample like every other draw's textures
        RefCntAutoPtr<ITexture> tex;
        CreateTextureFromFile(path, info, m_impl->device, &tex);
        if (!tex) { return TextureHandle::Invalid; } // failure leaves the out-pointer null, not a throw

        m_impl->textures.push_back(std::move(tex));
        return static_cast<TextureHandle>(m_impl->textures.size()); // 1-based; 0 = Invalid
    }

    // Releases one texture's GPU memory early (the thumbnail cache calls this at shutdown, not
    // per-frame). Leaves the slot null rather than compacting the vector: handles must stay
    // stable, and this cache is small enough that slot reuse isn't worth the bookkeeping.
    void Renderer::DestroyTexture(TextureHandle texture) {
        const uint32_t idx = static_cast<uint32_t>(texture);
        if (idx == 0 || idx > m_impl->textures.size()) { return; }
        m_impl->textures[idx - 1].Release();
    }

    // The texture's default SRV, handed out as a plain integer so this header never has to
    // mention ITextureView; the UI casts it to ImTextureID at the ImGui::Image call site.
    // Diligent's ImGui backend reinterpret_casts it right back and transitions the resource
    // itself (RESOURCE_STATE_TRANSITION_MODE_TRANSITION), the same path the model albedo
    // texture already goes through, so there's no extra state-management burden here.
    uint64_t Renderer::GetTextureImGuiID(TextureHandle texture) const {
        const uint32_t idx = static_cast<uint32_t>(texture);
        if (idx == 0 || idx > m_impl->textures.size() || !m_impl->textures[idx - 1]) { return 0; }
        return reinterpret_cast<uint64_t>(m_impl->textures[idx - 1]->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
    }

    // Pixel dimensions read straight from the texture's own desc; nothing cached, since
    // Diligent already stores it and the caller (a preview pane) only asks once per draw.
    void Renderer::GetTextureSize(TextureHandle texture, uint32_t &width, uint32_t &height) const {
        const uint32_t idx = static_cast<uint32_t>(texture);
        if (idx == 0 || idx > m_impl->textures.size() || !m_impl->textures[idx - 1]) { return; }
        const TextureDesc &desc = m_impl->textures[idx - 1]->GetDesc();
        width = desc.Width;
        height = desc.Height;
    }

    // --- Debug UI (Dear ImGui) --------------------------------------------------

    bool Renderer::InitUI(GLFWwindow *window) {
        // ImGuiImplDiligent's constructor calls ImGui::CreateContext(); it must
        // run before ImGui_ImplGlfw_InitForVulkan(), which itself calls
        // ImGui::GetIO() and asserts if no context exists yet.
        //
        // Build the ImGui PSO for how the UI is actually drawn (EndScene): to the back
        // buffer, with NO depth attachment. Passing the swap chain's depth format here
        // instead makes Diligent warn every frame that the bound DSV (none) doesn't
        // match the PSO, so pass TEX_FORMAT_UNKNOWN for depth.
        const SwapChainDesc &sc = m_impl->swapChain->GetDesc();
        ImGuiDiligentCreateInfo imguiCI{m_impl->device, sc.ColorBufferFormat, TEX_FORMAT_UNKNOWN};
        m_impl->imgui = std::make_unique<ImGuiImplDiligent>(imguiCI);

        if (!ImGui_ImplGlfw_InitForVulkan(window, /*install_callbacks=*/true)) {
            std::fprintf(stderr, "Renderer: ImGui GLFW backend init failed\n");
            m_impl->imgui.reset();
            return false;
        }
        return true;
    }

    void Renderer::ShutdownUI() {
        if (!m_impl->imgui) { return; }
        // Tear down in the reverse of InitUI. The GLFW platform backend must be shut
        // down *before* the ImGui context is destroyed: ~ImGuiImplDiligent() calls
        // ImGui::DestroyContext(), which asserts ("Forgot to shutdown Platform
        // backend?") if the GLFW backend is still registered; that assert aborts the
        // process on window close.
        ImGui_ImplGlfw_Shutdown();
        m_impl->imgui.reset(); // destroys the Diligent ImGui backend + the ImGui context
    }

    void Renderer::BeginUI() {
        ImGui_ImplGlfw_NewFrame();
        const SwapChainDesc &scDesc = m_impl->swapChain->GetDesc();
        m_impl->imgui->NewFrame(scDesc.Width, scDesc.Height, scDesc.PreTransform);
    }

    void Renderer::EndUI() { m_impl->imgui->Render(m_impl->context); }

} // namespace toon