//============================================================================
//  ui/panels/menu_bar.cpp: see menu_bar.h.
//============================================================================
#include "ui/panels/menu_bar.h"

#include "app/editor_state.h"
#include "app/scene_ops.h"

#include <GLFW/glfw3.h>

namespace toon {

    void DrawMenuBar(EditorState &state) {
        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("New Scene")) { NewScene(state); }
                if (ImGui::MenuItem("Open Scene...")) { state.openScenePopupRequested = true; }
                ImGui::Separator();
                if (ImGui::MenuItem("Save Scene")) { SaveSceneFrom(state, state.scenePathBuf); }
                if (ImGui::MenuItem("Save Scene As...")) { state.saveScenePopupRequested = true; }
                ImGui::Separator();
                if (ImGui::MenuItem("Exit")) { glfwSetWindowShouldClose(state.window, GLFW_TRUE); }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Edit")) {
                Scene &scene = state.scene;
                if (ImGui::MenuItem("Add Entity")) { scene.selected = AddChildEntity(scene, 0, "Entity"); }
                const bool hasSelection =
                    scene.selected > 0 && scene.selected < static_cast<int>(scene.entities.size());
                if (ImGui::MenuItem("Duplicate Entity", nullptr, false, hasSelection)) {
                    const int d = DuplicateEntity(scene, scene.selected);
                    if (d >= 0) { scene.selected = d; }
                }
                if (ImGui::MenuItem("Delete Entity", nullptr, false, hasSelection)) {
                    DeleteEntity(scene, scene.selected);
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Deselect", nullptr, false, scene.selected >= 0)) { scene.selected = -1; }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Tools")) {
                if (ImGui::MenuItem("Move", "W", state.gizmoOp == ImGuizmo::TRANSLATE)) {
                    state.gizmoOp = ImGuizmo::TRANSLATE;
                }
                if (ImGui::MenuItem("Rotate", "E", state.gizmoOp == ImGuizmo::ROTATE)) {
                    state.gizmoOp = ImGuizmo::ROTATE;
                }
                if (ImGui::MenuItem("Scale", "R", state.gizmoOp == ImGuizmo::SCALE)) {
                    state.gizmoOp = ImGuizmo::SCALE;
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Local Space", "X", state.gizmoMode == ImGuizmo::LOCAL)) {
                    state.gizmoMode = (state.gizmoMode == ImGuizmo::LOCAL) ? ImGuizmo::WORLD : ImGuizmo::LOCAL;
                }
                ImGui::Separator();
                ImGui::MenuItem("Run Scripts", nullptr, &state.runScripts);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("View")) {
                if (ImGui::BeginMenu("Themes")) {
                    for (int i = 0; i < static_cast<int>(Theme::Count); ++i) {
                        const Theme t = static_cast<Theme>(i);
                        if (ImGui::MenuItem(ThemeName(t), nullptr, t == state.uiTheme)) {
                            state.uiTheme = t;
                            ApplyTheme(state.uiTheme, state.uiScale, state.window);
                        }
                    }
                    ImGui::EndMenu();
                }
                ImGui::Separator();
                ImGui::MenuItem("Playback", nullptr, &state.showPlayback);
                ImGui::MenuItem("Objects", nullptr, &state.showHierarchy);
                ImGui::MenuItem("Properties", nullptr, &state.showInspector);
                ImGui::MenuItem("Settings", nullptr, &state.showDebug);
                ImGui::MenuItem("Contents", nullptr, &state.showAssetBrowser);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Help")) {
                if (ImGui::MenuItem("About ToonEngine")) { state.aboutPopupRequested = true; }
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }

        // Modals queued by the menu above. OpenPopup runs here, at the same ID-stack depth
        // BeginPopupModal below reads from -- see EditorState's *PopupRequested comment.
        if (state.openScenePopupRequested) {
            ImGui::OpenPopup("Open Scene");
            state.openScenePopupRequested = false;
        }
        if (state.saveScenePopupRequested) {
            ImGui::OpenPopup("Save Scene As");
            state.saveScenePopupRequested = false;
        }
        if (state.aboutPopupRequested) {
            ImGui::OpenPopup("About ToonEngine");
            state.aboutPopupRequested = false;
        }

        if (ImGui::BeginPopupModal("Open Scene", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::InputText("Path", state.scenePathBuf, sizeof(state.scenePathBuf));
            if (ImGui::Button("Open")) {
                LoadSceneInto(state, state.scenePathBuf);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) { ImGui::CloseCurrentPopup(); }
            ImGui::EndPopup();
        }
        if (ImGui::BeginPopupModal("Save Scene As", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::InputText("Path", state.scenePathBuf, sizeof(state.scenePathBuf));
            if (ImGui::Button("Save")) {
                SaveSceneFrom(state, state.scenePathBuf);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) { ImGui::CloseCurrentPopup(); }
            ImGui::EndPopup();
        }
        if (ImGui::BeginPopupModal("About ToonEngine", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("ToonEngine");
            ImGui::TextDisabled("A from-scratch, cross-platform toon-shaded game engine.");
            ImGui::Separator();
            ImGui::Text("Built on Diligent Engine (Vulkan) + GLFW + Dear ImGui.");
            if (ImGui::Button("Close")) { ImGui::CloseCurrentPopup(); }
            ImGui::EndPopup();
        }
    }

} // namespace toon
