#pragma once
//============================================================================
//  app/picking.h: click-to-select. Unproject the mouse to a world ray, then find
//  the nearest entity whose world-space bounds it pierces.
//
//  Geometric (ray-vs-bounding-box), NOT the physics PhysicsWorld::Raycast: that raycast's
//  bodies only exist while the sim is Playing (BuildPhysicsWorld runs at Play/Step, Stop
//  clears them -- app/physics_glue.cpp), and it only sees collider-bearing entities. Editor
//  selection needs to work in Editing mode too, and for every visible entity, collider or
//  not -- the same reason Unity/Unreal/Godot's editor picking is decoupled from their own
//  runtime physics raycast. See docs/roadmap.md's mouse-pick entry.
//============================================================================
#include "core/math.h" // Vec3

namespace toon {

    struct EditorState;
    struct Scene;
    class Renderer;

    // Fallback pick-box half-extent for a transformed entity with no mesh/model bounds (a
    // light or an empty anchor) -- app/editor_render.cpp draws a matching wireframe cube so
    // it's visibly clickable, not just a dead zone.
    constexpr float kPickBoxHalfExtent = 0.25f;

    // Nearest entity (root excluded) whose world-space bounds the ray pierces at t >= 0, or -1
    // if it hits nothing. A mesh/model entity's bounds come from the renderer (GetMeshBounds/
    // GetModelBounds, transformed by its worldMatrix); every other transformed entity gets the
    // fixed kPickBoxHalfExtent box above, centered on its world position.
    int PickEntity(const Scene &scene, const Renderer &renderer, const Vec3 &rayOrigin, const Vec3 &rayDir);

    // Click-to-select for the editor viewport: unprojects the current mouse position
    // (Renderer::ScreenPointToRay) and writes the PickEntity result to state.scene.selected
    // (-1 on empty space, matching the Objects panel's click-toggle-off convention). A no-op
    // on a drag (camera orbit/pan use the right/middle buttons, so this only ever guards
    // against a gizmo drag), over ImGui or the gizmo, or on anything but a left-button release.
    // Call once per frame, inside BeginUI/EndUI, before DrawGizmoOverlay (see main.cpp).
    void DoMousePicking(EditorState &state);

} // namespace toon
