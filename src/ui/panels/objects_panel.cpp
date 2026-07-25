//============================================================================
//  ui/panels/objects_panel.cpp: see objects_panel.h.
//============================================================================
#include "ui/panels/objects_panel.h"

#include "app/editor_state.h"

#include "imgui_internal.h" // ImGuiSelectableFlags_SpanAvailWidth (internal-only, not in imgui.h)

namespace toon {

    void DrawObjectsPanel(EditorState &state) {
        Scene &scene = state.runtime.scene;

        // Structural edits reorder the vector and invalidate indices, so the loop only
        // RECORDS a pending op / drop and applies them afterward.
        enum class HierOp { None, AddChild, Duplicate, Delete };
        HierOp pendingOp = HierOp::None;
        int pendingTarget = -1;
        enum class DropKind { Child, Before, After };
        int dropSrc = -1, dropDst = -1;
        DropKind dropKind = DropKind::Child;

        // hierarchyOpen captures the pre-Begin value: Begin(..., &state.showHierarchy) may flip
        // showHierarchy to false itself (the window's own close button), but Begin was still
        // CALLED this frame whenever we entered here, so End() below must still be paired
        // against that, not against showHierarchy's possibly-just-changed value.
        const bool hierarchyOpen = state.showHierarchy;
        if (hierarchyOpen && ImGui::Begin("Objects", &state.showHierarchy)) {
            const int n = static_cast<int>(scene.entities.size());
            for (int i = 0; i < n; ++i) {
                const Entity &e = scene.entities[i];
                const bool isRoot = (e.parent == -1);

                // Depth = length of the parent chain (drives the indent).
                int depth = 0;
                for (int p = e.parent, guard = 0; p >= 0 && p < n && guard < n; p = scene.entities[p].parent, ++guard) {
                    ++depth;
                }

                ImGui::PushID(i);
                if (depth > 0) { ImGui::Indent(depth * 16.0f); }

                const bool selected = (scene.selected == i);
                if (ImGui::Selectable(e.name.c_str(), selected, ImGuiSelectableFlags_SpanAvailWidth)) {
                    scene.selected = selected ? -1 : i; // click toggles selection off
                }

                // Drag source (everything but the root).
                if (!isRoot && ImGui::BeginDragDropSource()) {
                    ImGui::SetDragDropPayload("TOON_ENTITY_IDX", &i, sizeof(int));
                    ImGui::Text("%s", e.name.c_str());
                    ImGui::EndDragDropSource();
                }
                // Drop target: cursor-Y within the row picks the zone; top/bottom quarter =
                // sibling before/after, middle = make-child (the root only accepts children).
                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload *pl = ImGui::AcceptDragDropPayload("TOON_ENTITY_IDX")) {
                        const ImVec2 rmin = ImGui::GetItemRectMin();
                        const ImVec2 rmax = ImGui::GetItemRectMax();
                        const float frac = (ImGui::GetIO().MousePos.y - rmin.y) / (rmax.y - rmin.y);
                        dropSrc = *static_cast<const int *>(pl->Data);
                        dropDst = i;
                        if (isRoot) {
                            dropKind = DropKind::Child;
                        } else if (frac < 0.25f) {
                            dropKind = DropKind::Before;
                        } else if (frac > 0.75f) {
                            dropKind = DropKind::After;
                        } else {
                            dropKind = DropKind::Child;
                        }
                    }
                    ImGui::EndDragDropTarget();
                }
                // Right-click: structural ops (Duplicate/Delete disabled on the root).
                if (ImGui::BeginPopupContextItem()) {
                    scene.selected = i;
                    if (ImGui::MenuItem("Add Child")) {
                        pendingOp = HierOp::AddChild;
                        pendingTarget = i;
                    }
                    if (ImGui::MenuItem("Duplicate", nullptr, false, !isRoot)) {
                        pendingOp = HierOp::Duplicate;
                        pendingTarget = i;
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Delete", nullptr, false, !isRoot)) {
                        pendingOp = HierOp::Delete;
                        pendingTarget = i;
                    }
                    ImGui::EndPopup();
                }

                if (depth > 0) { ImGui::Unindent(depth * 16.0f); }
                ImGui::PopID();
            }
        }
        if (hierarchyOpen) { ImGui::End(); }

        // Apply the one recorded structural op, then the drag-drop; indices are stable now.
        switch (pendingOp) {
            case HierOp::AddChild:
                scene.selected = AddChildEntity(scene, pendingTarget, "Entity");
                break;
            case HierOp::Duplicate: {
                const int d = DuplicateEntity(scene, pendingTarget);
                if (d >= 0) { scene.selected = d; }
            } break;
            case HierOp::Delete:
                DeleteEntity(scene, pendingTarget);
                break;
            case HierOp::None:
                break;
        }
        if (dropSrc >= 0 && dropDst >= 0) {
            if (dropKind == DropKind::Child) {
                ReparentEntity(scene, dropSrc, dropDst);
            } else {
                MoveEntityAsSibling(scene, dropSrc, dropDst, dropKind == DropKind::Before);
            }
        }
    }

} // namespace toon
