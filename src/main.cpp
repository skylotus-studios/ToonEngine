//============================================================================
//  ToonEngine — entry point.
//
//  Owns the window and drives the editor's init/tick/render/UI calls (app/editor_*.h,
//  ui/panels/*.h). All GPU/backend work lives behind core/rendering/renderer.h (see that
//  header for the seam rationale); this file includes no Diligent header at all — that's
//  the point of the seam.
//============================================================================
#include "app/editor_init.h"
#include "app/editor_render.h"
#include "app/editor_state.h"
#include "app/editor_tick.h"
#include "app/picking.h"
#include "app/scene_ops.h"
#include "core/input/input_system.h"
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
#include <string>

int main() {
    if (!glfwInit()) {
        std::fprintf(stderr, "GLFW init failed\n");
        return 1;
    }

    // We render with Vulkan, so tell GLFW not to create an OpenGL context.
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    // Start maximized so the editor fills the screen and the right-docked panels (Properties /
    // Settings) stay on-screen on any monitor. Creating oversize (e.g. 3840x2160 on a smaller
    // display) pushed the dock layout's right column off the visible area. The 1600x900 below
    // is just the restored-down size.
    glfwWindowHint(GLFW_MAXIMIZED, GLFW_TRUE);

    GLFWwindow *window = glfwCreateWindow(1600, 900, "ToonEngine", nullptr, nullptr);
    if (!window) {
        std::fprintf(stderr, "Failed to create window\n");
        glfwTerminate();
        return 1;
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

        state.renderer.BeginUI();
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
            if (const std::string activated = toon::RenderFileBrowser(state.assetBrowser, state.renderer);
                !activated.empty() && std::filesystem::path(activated).extension() == ".scene") {
                toon::LoadSceneInto(state, activated.c_str());
            }
        }

        state.renderer.EndUI();
        state.renderer.EndFrame();
    }

    toon::Input::Shutdown();
    toon::ShutdownFileBrowser(state.assetBrowser,
                              state.renderer); // frees cached thumbnails; must run before the device does
    state.physicsWorld.Shutdown();
    state.renderer.Shutdown();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
