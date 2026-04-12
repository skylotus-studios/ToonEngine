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
  main.cpp                  Entry point, GLFW window, game loop, FBO, render dispatch
  core/                     Low-level GPU abstractions
    mesh.h/.cpp             VAO/VBO/EBO, flexible vertex layouts
    shader.h/.cpp           Compile/link from files, hot-reload via timestamps
    texture.h/.cpp          stb_image loading (file + memory), GL texture management
    material.h              Base color (vec4) + optional texture per sub-mesh
    transform.h/.cpp        Position/rotation/scale -> model matrix (TRS)
    animation.h             Joint, Skeleton, AnimationClip, keyframe types
    animator.h/.cpp         Playback, keyframe interpolation, joint matrix computation
  scene/                    Scene graph and asset loading
    scene.h                 Entity (transform, meshes, skeleton, animator, light), Scene
    camera.h/.cpp           FPS fly camera (right-click + WASD)
    model_loader.h/.cpp     glTF (cgltf) + FBX (ufbx), skinning, skeleton, animations
  ui/                       Debug tooling
    overlay.h/.cpp          ImGui panels: render settings + entity list/inspector + anim controls
assets/shaders/
  model.vert                Shared vertex shader (MVP + GPU skinning via uJoints[128])
  toon.frag                 Multi-light cel shading + shadow tint + rim lighting
  outline.*                 Inverted hull outlines (with skinning support)
  edge.*                    Sobel edge detection post-process (depth-based)
  triangle.*                Vertex-colored demo geometry
  model.frag                Plain diffuse fallback
libs/
  cgltf/                    Single-header glTF 2.0 loader
  ufbx/                     Single-file FBX loader
```

## Render pipeline

Scene renders to an FBO when Sobel edge detection is on, otherwise straight to screen.

1. **Scene pass** -- iterates `Scene::entities`, dispatches by `ShadingMode`:
   - *VertexColor*: `triangle.*` shaders
   - *Toon*: front faces with `toon.frag` (multi-light cel + rim + shadow tint), back faces with `outline.*`
   - Light entities (directional/point) are gathered and uploaded as uniform arrays (max 8)
   - Skinned entities upload `uJoints[]` and set `uSkinned=true` for GPU skinning
2. **Post-process** (optional) -- fullscreen Sobel on linearized depth via `edge.*`
3. **ImGui overlay** -- render settings, entity inspector, animation controls

## Conventions

- Target-based CMake only. Dependencies via vcpkg manifest; single-header libs in `libs/`.
- `src/` is the include root. Cross-directory includes use `"core/mesh.h"`, `"scene/scene.h"`, etc.
- Shader hot-reload: edit `.vert`/`.frag` while running, changes apply next frame.
- Model loading via argv[1]. Auto-fit normalizes to 1 unit at origin.
- Per-mesh materials: glTF per-primitive PBR; FBX split by `material_parts`.
- Vertex layout: non-skinned = pos+norm+uv (stride 32), skinned = +boneIds+weights (stride 64).
- Skeletal animation: max 128 joints (uniform array). Keyframe interpolation (lerp/slerp) with hierarchy walk. glTF reads JOINTS_0/WEIGHTS_0 accessors; FBX uses ufbx bake API.
- Fixed timestep (60 Hz) for simulation, variable-rate rendering.

## Constraints

- **GL 4.1 only** -- macOS ceiling. No DSA, no compute, no SSBOs.
- **glTF + FBX only** -- no OBJ or other formats.
- Clang everywhere. MSVC and GCC are not tested.

## What's next

- Shadow mapping
- Normal-based edge detection (Sobel on normals, complementing depth edges)
- Specular highlight band (toon-style specular lobe)
- Normal map support (sample normal texture in toon.frag)
- Animation blending / crossfade between clips
