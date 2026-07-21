//============================================================================
//  ui/panels/properties_panel.cpp: see properties_panel.h.
//============================================================================
#include "ui/panels/properties_panel.h"

#include "app/editor_state.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace toon {

    void DrawPropertiesPanel(EditorState &state) {
        Scene &scene = state.scene;

        // inspectorOpen captures the pre-Begin value: see objects_panel.cpp's hierarchyOpen
        // comment for why.
        const bool inspectorOpen = state.showInspector;
        if (inspectorOpen && ImGui::Begin("Properties", &state.showInspector)) {
            if (scene.selected < 0 || scene.selected >= static_cast<int>(scene.entities.size())) {
                ImGui::TextDisabled("Select an entity in the hierarchy.");
            } else {
                Entity &e = scene.entities[scene.selected];
                const bool isRoot = (e.parent == -1);

                char nameBuf[128];
                std::snprintf(nameBuf, sizeof(nameBuf), "%s", e.name.c_str());
                if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf))) { e.name = nameBuf; }

                // Transform: rotation shown in DEGREES for editing, stored as a quaternion
                // (core/rendering/renderer.h's Transform::rotation); QuatToEuler/QuatFromEuler
                // (core/math.h) convert at this widget boundary only. Euler is re-derived
                // from the live quaternion every frame rather than cached, so a value can
                // display renormalized (e.g. 190 shown as -170) and, near gimbal lock, the
                // other two axes can jump when one is edited, the same trade-off Unity's
                // inspector accepts without its extra hidden-Euler-cache bookkeeping.
                if (e.transform && !isRoot) {
                    ImGui::SeparatorText("Transform");
                    Transform &t = *e.transform;
                    constexpr float kRad2Deg = 57.29578f, kDeg2Rad = 0.01745329f;
                    ImGui::DragFloat3("Position", &t.position.x, 0.01f);
                    const Vec3 eulerRad = QuatToEuler(t.rotation);
                    float deg[3] = {eulerRad.x * kRad2Deg, eulerRad.y * kRad2Deg, eulerRad.z * kRad2Deg};
                    if (ImGui::DragFloat3("Rotation", deg, 0.5f)) {
                        t.rotation = QuatFromEuler({deg[0] * kDeg2Rad, deg[1] * kDeg2Rad, deg[2] * kDeg2Rad});
                    }
                    ImGui::DragFloat3("Scale", &t.scale.x, 0.01f, 0.001f, 100.0f);
                } else if (isRoot) {
                    ImGui::TextDisabled("(scene root: a pure anchor, no transform)");
                }

                // Material: only for renderables (a mesh or a model).
                if (e.mesh != MeshHandle::Invalid || e.model != ModelHandle::Invalid) {
                    ImGui::SeparatorText("Material");
                    ImGui::ColorEdit3("Base color", &e.material.baseColor.x);
                    ImGui::ColorEdit3("Outline color", &e.material.outlineColor.x);
                    ImGui::DragFloat("Outline width", &e.material.outlineWidth, 0.001f, 0.0f, 0.5f, "%.3f");
                    ImGui::SliderFloat("Roughness", &e.material.roughness, 0.0f, 1.0f);
                }

                // Light: a true optional component (core/scene/scene.h); Add/Remove it
                // directly, rather than assuming it's attached in code. Direction isn't a
                // field here: it comes from the entity's rotation (aim it with the gizmo,
                // like Material's transform above).
                if (e.light) {
                    ImGui::SeparatorText("Light");
                    if (ImGui::Button("Remove Light")) {
                        e.light.reset();
                    } else {
                        ImGui::ColorEdit3("Color", &e.light->color.x);
                        ImGui::DragFloat("Intensity", &e.light->intensity, 0.01f, 0.0f, 10.0f, "%.2f");
                        ImGui::TextDisabled("Aim: rotate this entity (gizmo R).");
                    }
                } else {
                    ImGui::SeparatorText("Light");
                    if (ImGui::Button("Add Light")) { e.light = LightComponent{}; }
                }

                // Collider and Rigid Body (M2.1): two fully independent optional
                // components (core/scene/scene.h), each Add/Remove-able on its own; neither
                // gates the other's visibility here, even though a RigidBody only does
                // anything once the entity also has a Collider (app/physics_glue.cpp's
                // BuildPhysicsWorld silently skips a body with no collider). Both need a
                // transform to be placed at, same gate as the Transform section above.
                // Edits here only take effect on the NEXT Play session -- the physics world
                // is (re)built once when Play starts, not continuously re-read from these
                // fields while it's running.
                if (e.transform && !isRoot) {
                    ImGui::SeparatorText("Collider");
                    if (e.collider) {
                        if (ImGui::Button("Remove Collider")) {
                            e.collider.reset();
                        } else {
                            const char *kShapeNames[] = {"Box", "Sphere", "Capsule"};
                            int shapeIdx = static_cast<int>(e.collider->shape);
                            if (ImGui::Combo("Shape", &shapeIdx, kShapeNames, IM_ARRAYSIZE(kShapeNames))) {
                                e.collider->shape = static_cast<ColliderShape>(shapeIdx);
                            }
                            switch (e.collider->shape) {
                                case ColliderShape::Box:
                                    ImGui::DragFloat3("Half-extents", &e.collider->extents.x, 0.01f, 0.001f, 100.0f);
                                    break;
                                case ColliderShape::Sphere:
                                    ImGui::DragFloat("Radius", &e.collider->extents.x, 0.01f, 0.001f, 100.0f);
                                    break;
                                case ColliderShape::Capsule:
                                    ImGui::DragFloat("Half-height", &e.collider->extents.x, 0.01f, 0.001f, 100.0f);
                                    ImGui::DragFloat("Radius", &e.collider->extents.y, 0.01f, 0.001f, 100.0f);
                                    break;
                            }
                        }
                    } else {
                        if (ImGui::Button("Add Collider")) { e.collider = ColliderComponent{}; }
                    }

                    ImGui::SeparatorText("Rigid Body");
                    if (e.body) {
                        if (ImGui::Button("Remove Rigid Body")) {
                            e.body.reset();
                        } else {
                            const char *kTypeNames[] = {"Static", "Dynamic", "Kinematic"};
                            int typeIdx = static_cast<int>(e.body->type);
                            if (ImGui::Combo("Type", &typeIdx, kTypeNames, IM_ARRAYSIZE(kTypeNames))) {
                                e.body->type = static_cast<BodyType>(typeIdx);
                            }
                            ImGui::BeginDisabled(e.body->type != BodyType::Dynamic); // ignored otherwise
                            ImGui::DragFloat("Mass", &e.body->mass, 0.01f, 0.001f, 1000.0f);
                            ImGui::EndDisabled();
                            ImGui::DragFloat("Friction", &e.body->friction, 0.01f, 0.0f, 2.0f);
                            ImGui::DragFloat("Restitution", &e.body->restitution, 0.01f, 0.0f, 1.0f);
                        }
                    } else {
                        if (ImGui::Button("Add Rigid Body")) { e.body = RigidBodyComponent{}; }
                    }
                }

                // Audio Source (M2.2): a true optional component (core/scene/scene.h), same
                // Add/Remove idiom as Light/Collider/Rigid Body above. Field edits here only
                // take effect on the NEXT Play session (BuildAudioWorld builds the handled
                // sound once, like BuildPhysicsWorld does for bodies); "Preview" is the
                // exception: it auditions the clip immediately, in ANY mode, independent of
                // Play/Pause/Stop, using the component's OWN volume/pitch/loop/spatial fields
                // (a real Play()/Stop() pair, held in state.previewHandle -- see
                // editor_state.h's comment) so a looping source previews as a loop until
                // stopped, not a single one-shot.
                if (e.transform && !isRoot) {
                    ImGui::SeparatorText("Audio Source");
                    if (e.audioSource) {
                        if (ImGui::Button("Remove Audio Source")) {
                            e.audioSource.reset();
                        } else {
                            char clipBuf[256];
                            std::snprintf(clipBuf, sizeof(clipBuf), "%s", e.audioSource->clip.c_str());
                            if (ImGui::InputText("Clip", clipBuf, sizeof(clipBuf))) { e.audioSource->clip = clipBuf; }
                            ImGui::SameLine();
                            const bool previewingThis = (state.previewHandle != SoundHandle::Invalid) &&
                                                        (state.previewEntityIdx == scene.selected);
                            if (ImGui::Button(previewingThis ? "Stop Preview" : "Preview")) {
                                if (state.previewHandle != SoundHandle::Invalid) {
                                    state.audio.Stop(state.previewHandle);
                                    state.previewHandle = SoundHandle::Invalid;
                                    state.previewEntityIdx = -1;
                                }
                                if (!previewingThis) {
                                    SoundDesc desc;
                                    desc.clip = e.audioSource->clip;
                                    desc.volume = e.audioSource->volume;
                                    desc.pitch = e.audioSource->pitch;
                                    desc.loop = e.audioSource->loop;
                                    desc.spatial = e.audioSource->spatial;
                                    desc.stream = e.audioSource->stream;
                                    desc.maxDistance = e.audioSource->maxDistance;
                                    desc.position = {e.worldMatrix.m[12], e.worldMatrix.m[13], e.worldMatrix.m[14]};
                                    state.previewHandle = state.audio.Play(desc);
                                    state.previewEntityIdx = scene.selected;
                                }
                            }
                            ImGui::SliderFloat("Volume", &e.audioSource->volume, 0.0f, 1.0f);
                            ImGui::DragFloat("Pitch", &e.audioSource->pitch, 0.01f, 0.1f, 4.0f);
                            ImGui::Checkbox("Loop", &e.audioSource->loop);
                            ImGui::SameLine();
                            ImGui::Checkbox("Autoplay", &e.audioSource->autoplay);
                            ImGui::Checkbox("Spatial", &e.audioSource->spatial);
                            ImGui::SameLine();
                            ImGui::Checkbox("Stream", &e.audioSource->stream);
                            ImGui::BeginDisabled(!e.audioSource->spatial); // meaningless for a non-spatial source
                            ImGui::DragFloat("Max Distance", &e.audioSource->maxDistance, 0.1f, 0.1f, 200.0f);
                            ImGui::EndDisabled();
                        }
                    } else {
                        if (ImGui::Button("Add Audio Source")) { e.audioSource = AudioSource{}; }
                    }
                }

                // Skeletal animation (roadmap #11): a true optional component (core/scene/
                // scene.h), same Add/Remove idiom as the sections above. Only offered when the
                // entity actually has a SKINNED model (Renderer::ModelHasSkin) -- a procedural
                // mesh or an unskinned model (e.g. the helmet) has no clip list to pick from.
                // The clip combo reads names straight from the loaded model (Renderer::
                // GetModelAnimationCount/Name), so it always matches the actual file; nothing
                // here is authored data beyond which clip/playing/looping to use (see
                // AnimationComponent's own comment on why time/prevTime aren't editable here).
                if (e.transform && !isRoot && e.model != ModelHandle::Invalid && state.renderer.ModelHasSkin(e.model)) {
                    ImGui::SeparatorText("Animation");
                    if (e.animation) {
                        if (ImGui::Button("Remove Animation")) {
                            e.animation.reset();
                        } else {
                            const uint32_t clipCount = state.renderer.GetModelAnimationCount(e.model);
                            if (clipCount == 0) {
                                ImGui::TextDisabled("(model has a skin but no animation clips)");
                            } else {
                                const int clampedIdx = std::clamp(e.animation->clipIndex, 0,
                                                                  static_cast<int>(clipCount) - 1);
                                std::string currentName =
                                    state.renderer.GetModelAnimationName(e.model, static_cast<uint32_t>(clampedIdx));
                                if (currentName.empty()) { currentName = "Clip " + std::to_string(clampedIdx); }
                                if (ImGui::BeginCombo("Clip", currentName.c_str())) {
                                    for (uint32_t ci = 0; ci < clipCount; ++ci) {
                                        std::string name = state.renderer.GetModelAnimationName(e.model, ci);
                                        if (name.empty()) { name = "Clip " + std::to_string(ci); }
                                        const bool isSelected = (static_cast<int>(ci) == e.animation->clipIndex);
                                        if (ImGui::Selectable(name.c_str(), isSelected)) {
                                            e.animation->clipIndex = static_cast<int>(ci);
                                            e.animation->time = 0.0f;
                                            e.animation->prevTime = 0.0f;
                                        }
                                        if (isSelected) { ImGui::SetItemDefaultFocus(); }
                                    }
                                    ImGui::EndCombo();
                                }
                            }
                            ImGui::Checkbox("Playing", &e.animation->playing);
                            ImGui::SameLine();
                            ImGui::Checkbox("Looping", &e.animation->looping);
                        }
                    } else {
                        if (ImGui::Button("Add Animation")) {
                            // Preselect clip 0 rather than leaving the generic "-1 = none"
                            // sentinel (see AnimationComponent's own comment): adding the
                            // component should show something moving immediately, not a
                            // combo that looks active over a still-static bind pose.
                            AnimationComponent a;
                            if (state.renderer.GetModelAnimationCount(e.model) > 0) { a.clipIndex = 0; }
                            e.animation = a;
                        }
                    }
                }

                // Scripts (core/scene/script.h): a vector, not a single optional component;
                // an entity can carry several independent scripts at once (e.g. a Health
                // script alongside a PlayerMovement script), so each attached script gets its
                // own Remove button, and "Add Script" picks a registered type by name (the
                // same registry CreateScript/serialization use) rather than a single
                // attach/detach toggle. Editing a script's OWN fields (e.g. SpinScript's
                // axis/speed) is deliberately not exposed here; that needs Script to grow
                // its own ImGui-drawing hook (beyond Save/Load), a separate, larger feature
                // this pass doesn't include.
                if (e.transform && !isRoot) {
                    ImGui::SeparatorText("Scripts");

                    int pendingRemove = -1;
                    for (int si = 0; si < static_cast<int>(e.scripts.size()); ++si) {
                        ImGui::PushID(si);
                        ImGui::TextUnformatted(e.scripts[si].name.c_str());
                        ImGui::SameLine();
                        if (ImGui::Button("Remove")) { pendingRemove = si; }
                        ImGui::PopID();
                    }
                    if (pendingRemove >= 0) { e.scripts.erase(e.scripts.begin() + pendingRemove); }

                    const std::vector<std::string> availableScripts = GetRegisteredScriptNames();
                    if (availableScripts.empty()) {
                        ImGui::TextDisabled("(no script types registered)");
                    } else {
                        static int addScriptTypeIdx = 0;
                        addScriptTypeIdx = std::min(addScriptTypeIdx, static_cast<int>(availableScripts.size()) - 1);
                        if (ImGui::BeginCombo("##AddScriptType", availableScripts[addScriptTypeIdx].c_str())) {
                            for (int ti = 0; ti < static_cast<int>(availableScripts.size()); ++ti) {
                                const bool isSelected = (ti == addScriptTypeIdx);
                                if (ImGui::Selectable(availableScripts[ti].c_str(), isSelected)) {
                                    addScriptTypeIdx = ti;
                                }
                                if (isSelected) { ImGui::SetItemDefaultFocus(); }
                            }
                            ImGui::EndCombo();
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("Add Script")) {
                            const std::string &typeName = availableScripts[addScriptTypeIdx];
                            if (auto instance = CreateScript(typeName)) {
                                e.scripts.push_back({typeName, std::move(instance)});
                            }
                        }
                    }
                }
            }
        }
        if (inspectorOpen) { ImGui::End(); }
    }

} // namespace toon
