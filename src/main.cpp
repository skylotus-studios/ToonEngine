//============================================================================
//  ToonEngine: entry point.
//
//  Owns the window and drives the editor's init/tick/render/UI calls (app/editor_*.h,
//  ui/panels/*.h). All GPU/backend work lives behind core/rendering/renderer.h (see that
//  header for the seam rationale); this file includes no Diligent header at all, which is
//  the point of the seam.
//============================================================================
#include "app/editor_init.h"
#include "app/editor_render.h"
#include "app/editor_state.h"
#include "app/editor_tick.h"
#include "app/picking.h"
#include "app/runtime_init.h" // --play: run the standalone runtime instead of the editor
#include "app/scene_ops.h"
#include "core/input/input_system.h"
#include "core/platform/paths.h" // Assets::Init + Assets::Scenes (exe-relative asset paths)
#include "ui/panels/dockspace.h"
#include "ui/panels/gizmo_overlay.h"
#include "ui/panels/menu_bar.h"
#include "ui/panels/objects_panel.h"
#include "ui/panels/playback_panel.h"
#include "ui/panels/properties_panel.h"
#include "ui/panels/settings_panel.h"

// GLFW_INCLUDE_NONE is set engine-wide (CMakeLists.txt) since core/input/input_device.h
// (reachable through app/editor_state.h) pulls <GLFW/glfw3.h> too, ahead of this file's
// own include below.
#include <GLFW/glfw3.h>

#include <cstdio>
#include <filesystem> // .scene extension check, routing an asset-browser double-click through LoadSceneInto
#include <iostream>
#include <string>

int main(int argc, char **argv) {
    // Diligent buffers its own logging through std::cout; a hang or a silent early-return
    // init failure can otherwise lose whatever it already printed (see MEMORY.md's glTF
    // loading gotchas). Unbuffered is negligible cost for a windowed editor's own startup
    // logging.
    std::cout.setf(std::ios::unitbuf);

    // Resolve the asset root before anything reads a path from it (the default scene below is
    // the first). Prefers assets/ next to the exe (a packaged build), else the baked source
    // tree; see core/platform/paths.h.
    toon::Assets::Init();

    // Roadmap #15: `ToonEngine.exe --play [scene]` runs the standalone runtime (no editor
    // chrome) instead of the editor -- a dev convenience that exercises the same code path
    // ToonPlayer.exe ships. Parsed here so the editor path below is untouched.
    const bool runtimeMode = (argc > 1 && std::string(argv[1]) == "--play");
    const std::string runtimeScene = (argc > 2) ? std::string(argv[2]) : toon::Assets::Scenes() + "/default.scene";

    if (!glfwInit()) {
        std::fprintf(stderr, "GLFW init failed\n");
        return 1;
    }

    // We render with Vulkan, so tell GLFW not to create an OpenGL context.
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    // The editor starts maximized so its docked panels stay on-screen on any monitor; the
    // runtime opens at the restored-down size (a game window, not an editor).
    if (!runtimeMode) { glfwWindowHint(GLFW_MAXIMIZED, GLFW_TRUE); }

    GLFWwindow *window = glfwCreateWindow(1600, 900, "ToonEngine", nullptr, nullptr);
    if (!window) {
        std::fprintf(stderr, "Failed to create window\n");
        glfwTerminate();
        return 1;
    }

    if (runtimeMode) {
        toon::RuntimeState rs;
        if (!toon::InitRuntime(rs, window, runtimeScene.c_str())) {
            glfwDestroyWindow(window);
            glfwTerminate();
            return 1;
        }
        toon::RunRuntimeLoop(rs);
        toon::ShutdownRuntime(rs);
        glfwDestroyWindow(window);
        glfwTerminate();
        return 0;
    }

    toon::EditorState state;
    if (!toon::InitEditor(state, window)) {
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    while (!glfwWindowShouldClose(window)) {
        toon::TickEditor(state);
        toon::RenderFrame(state);

        state.runtime.renderer.BeginUI();
        toon::GizmoHotkeys(state);
        toon::DrawMenuBar(state);
        toon::SetupDockspace(state);
        toon::DrawPlaybackPanel(state);
        toon::DrawObjectsPanel(state);
        toon::DrawPropertiesPanel(state);
        toon::DoMousePicking(state);
        toon::DrawGizmoOverlay(state);
        toon::DrawSettingsPanel(state);

        // Contents: passive navigation/preview, except a double-clicked .scene file, which
        // loads through the same path as the File menu's Open Scene (see app/scene_ops.h's
        // LoadSceneInto). FileBrowser::Render owns its Begin/End internally (no p_open
        // param), so unlike the panels above, hiding it via the View menu has no in-panel
        // close button.
        if (state.showAssetBrowser) {
            if (const std::string activated = toon::RenderFileBrowser(state.assetBrowser, state.runtime.renderer);
                !activated.empty() && std::filesystem::path(activated).extension() == ".scene") {
                toon::LoadSceneInto(state, activated.c_str());
            }
        }

        state.runtime.renderer.EndUI();
        state.runtime.renderer.EndFrame();
    }

    toon::Input::Shutdown();
    toon::ShutdownFileBrowser(state.assetBrowser,
                              state.runtime.renderer); // frees cached thumbnails; must run before the device does
    state.runtime.physicsWorld.Shutdown();
    state.runtime.renderer.Shutdown();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
