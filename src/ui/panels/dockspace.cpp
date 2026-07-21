//============================================================================
//  ui/panels/dockspace.cpp: see dockspace.h.
//============================================================================
#include "ui/panels/dockspace.h"

#include "app/editor_state.h"

#ifdef IMGUI_HAS_DOCK
#include "imgui_internal.h" // DockBuilder API, for the one-time default layout
#endif

namespace toon {

    void SetupDockspace(EditorState &state) {
#ifdef IMGUI_HAS_DOCK
        // DockSpaceOverViewport reads the main viewport's WorkPos/WorkSize, which Dear ImGui
        // already shrinks around the main menu bar (DrawMenuBar, submitted earlier this frame),
        // so this whole dockspace, Playback strip included, composes below it automatically;
        // no coordinate math needed here.
        const ImGuiID dockspaceId =
            ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);
        if (!state.dockLayoutBuilt) {
            state.dockLayoutBuilt = true;
            ImGui::DockBuilderRemoveNode(dockspaceId);
            ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
            ImGui::DockBuilderSetNodeSize(dockspaceId, ImGui::GetMainViewport()->Size);
            // Objects on the far left; Properties (top) + Settings (bottom) stacked on the
            // right; a thin Playback strip (M1.2) -- one row tall, just the transport buttons;
            // the gizmo Local/Snap controls live in Settings, not here -- sits above the
            // remaining center, so it reads as a sliver between Objects and Properties/Settings
            // rather than a bar spanning the whole width. Left/Right split FIRST so Playback's
            // Up split then only carves the narrower center strip left over.
            ImGuiID centerId = dockspaceId;
            const ImGuiID leftId = ImGui::DockBuilderSplitNode(centerId, ImGuiDir_Left, 0.20f, nullptr, &centerId);
            ImGuiID rightId = ImGui::DockBuilderSplitNode(centerId, ImGuiDir_Right, 0.34f, nullptr, &centerId);
            const ImGuiID playbackId = ImGui::DockBuilderSplitNode(centerId, ImGuiDir_Up, 0.0415f, nullptr, &centerId);
            const ImGuiID rightTopId = ImGui::DockBuilderSplitNode(rightId, ImGuiDir_Up, 0.55f, nullptr, &rightId);
            const ImGuiID bottomId = ImGui::DockBuilderSplitNode(centerId, ImGuiDir_Down, 0.28f, nullptr, &centerId);
            ImGui::DockBuilderDockWindow("Playback", playbackId);
            ImGui::DockBuilderDockWindow("Objects", leftId);
            ImGui::DockBuilderDockWindow("Properties", rightTopId);
            ImGui::DockBuilderDockWindow("Settings", rightId);
            ImGui::DockBuilderDockWindow("Contents", bottomId);
            // Playback reads as a fixed toolbar, not a document -- no tab/title to show or drag.
            if (ImGuiDockNode *playbackNode = ImGui::DockBuilderGetNode(playbackId)) {
                playbackNode->SetLocalFlags(ImGuiDockNodeFlags_NoTabBar);
            }
            ImGui::DockBuilderFinish(dockspaceId);
        }
#else
        (void)state;
#endif
    }

} // namespace toon
