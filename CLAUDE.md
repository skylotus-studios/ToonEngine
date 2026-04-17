# ToonEngine

From-scratch OpenGL 4.1 game engine focused on stylized/toon rendering.

## Build

Clang everywhere (clang-cl on Windows, Apple Clang on macOS). CMake 3.21+ with Presets, vcpkg manifest mode, Ninja, C++20.

```
cmake --preset windows-debug
cmake --build --preset windows-debug
./build/windows-debug/ToonEngine.exe [model_path]
```

## Source layout

```
src/
  main.cpp                  Entry point, game loop, FBO, shadow/grid passes, render dispatch
  core/                     Low-level GPU abstractions
    mesh.h/.cpp             VAO/VBO/EBO, flexible vertex layouts (explicit location support)
    shader.h/.cpp           Compile/link from files, hot-reload via timestamps
    texture.h/.cpp          stb_image loading + programmatic test textures (checker, normal map)
    material.h              Base color + texture + normal map per sub-mesh
    transform.h/.cpp        Position/rotation/scale -> model matrix (TRS)
    animation.h             Joint, Skeleton, AnimationClip, keyframe types
    animator.h/.cpp         Playback, keyframe interpolation, joint matrix computation
    input/                  Unified input system (keyboard, mouse, gamepad)
      keycodes.h            Key/Button/Axis enums, GLFW ↔ engine conversion
      input_device.h/.cpp   Per-device raw state (Keyboard, Mouse, Gamepad)
      input_event.h         Tagged-union InputEvent for the per-frame event queue
      input_system.h/.cpp   Init, BeginFrame, polling API, capture gate, event stream
      action_map.h/.cpp     Named actions/axes, bindings, runtime rebind, context stack
      binding_io.h/.cpp     JSON save/load for bindings (nlohmann-json)
  scene/                    Scene graph and asset loading
    scene.h                 Entity (meshes, skeleton, light, animator, modelPath), Scene
    camera.h/.cpp           Editor camera (orbit, pan, zoom, fly, focus)
    model_loader.h/.cpp     glTF + FBX: meshes, materials, skinning, skeleton, animations
    serializer.h/.cpp       Save/load scene to .scene text files
  ui/                       Debug tooling
    overlay.h/.cpp          ImGui docking + panels + ImGuizmo transform gizmo
assets/shaders/
  model.vert                Shared vertex shader (MVP + GPU skinning)
  toon.frag                 Multi-light cel + CSM + specular + normal map + shadow tint + rim
  outline.*                 Inverted hull outlines (smooth normals, screen-space option)
  shadow.*                  Depth-only CSM pass (with skinning)
  grid.frag                 Sky gradient + infinite ground grid (ray-plane intersection)
  fullscreen.vert           Attributeless fullscreen triangle (used by grid.frag)
  triangle.*                Vertex-colored demo geometry
libs/
  cgltf/                    Single-header glTF 2.0 loader
  ufbx/                     Single-file FBX loader
  imguizmo/                 Transform gizmo (translate/rotate/scale) for ImGui
```

## Render pipeline

1. **Shadow pass** -- 4-cascade CSM from the first directional light into `GL_TEXTURE_2D_ARRAY`
2. **Grid + sky pass** -- fullscreen ray-march: sky gradient above horizon, infinite grid at Y=0 with correct depth writes (major/minor lines, distance fade)
3. **Scene pass** -- direct to the default framebuffer:
   - *Toon*: front faces with `toon.frag`, back faces with `outline.*`
   - Light entities (directional/point, max 8) uploaded as uniform arrays
   - Skinned entities upload `uJoints[]` for GPU skinning
4. **ImGui overlay**

## Input system

All GLFW input callbacks live in `core/input/`. No other file calls `glfwGetKey` or `glfwSetMouseButtonCallback`.

- **Device layer** (`Keyboard`, `Mouse`, `Gamepad`): raw per-frame state with `current`/`previous` arrays for edge detection (`WasPressed`, `WasReleased`).
- **Polling API** (`Input::IsKeyDown`, `MouseDelta`, `ScrollDelta`, etc.): respects a capture gate so ImGui interaction suppresses engine input.
- **Event queue** (`Input::Events()`, `EachEvent(fn)`): per-frame stream of typed events (key/mouse/scroll/gamepad). Cleared on `BeginFrame`.
- **Action/axis layer**: named actions (`"camera.focus"`) and axes (`"camera.fly.forward"`) with arbitrary bindings (key, mouse button, gamepad button/axis). Queried via `IsActionDown`, `WasActionPressed`, `GetAxis`.
- **Context stack** (`PushContext`/`PopContext`): named sets of action bindings; top-of-stack wins. Default context is `"editor"`.
- **Persistence**: `assets/input.json` stores bindings per context. Loaded on startup; hardcoded defaults used as fallback.
- **Frame order**: `Input::BeginFrame()` → `glfwPollEvents()` → consume. Gamepads polled in `BeginFrame`.

## Conventions

- Target-based CMake only. Dependencies via vcpkg manifest; single-header libs in `libs/`.
- `src/` is the include root. Cross-directory includes: `"core/mesh.h"`, `"scene/scene.h"`, etc.
- Shader hot-reload: edit `.vert`/`.frag` while running, changes apply next frame.
- Model loading via argv[1]. Auto-fit normalizes to 1 unit at origin.
- Per-mesh materials: base color + texture + normal map. Normal maps use derivative-based TBN.
- Vertex layout: non-skinned = pos+norm+uv+smoothNorm (stride 44), skinned = +boneIds+weights (stride 76).
- Skeletal animation: max 128 joints. Bind-pose local TRS as defaults for un-animated joints.
- CSM: 4 cascades, 2048x2048, log-linear split, `sampler2DArrayShadow`, per-cascade bias.
- Scene serialization: `.scene` text files with entity transforms, lights, model paths. Save/Load via overlay buttons. Models re-loaded from disk on load.
- glTF textures loaded with flipY=false (matches glTF UV convention).
- Fixed timestep (60 Hz) for simulation, variable-rate rendering.

## Constraints

- **GL 4.1 only** -- macOS ceiling. No DSA, no compute, no SSBOs.
- **glTF + FBX only** -- no OBJ or other formats.
- Clang everywhere. MSVC and GCC are not tested.

## Engine Roadmap TODO Features

- Animation blending / crossfade between clips
- Audio system
- Physics / collision detection
- Particle system (toon-style)
- Multi-object scene editing (add/remove entities from overlay)
- Morph target / blend shape animation

## ImGUI TODO Features
- Undo/redo stack for property changes
- Material editor
- Per-entity material override panel
- Texture preview thumbnails in the inspector
- Drag-drop image files onto material slots
- File browser panel that shows assets/ contents
- Drag-drop models into the scene to create entities
- Texture/model preview on hover
- Render stats panel (draw calls, triangle count, shadow pass time, FBO switches)
- GPU timer queries for per-pass profiling
- Status bar (camera position, FPS, selected entity name)
- Keyboard shortcuts overlay (F1 or ?)