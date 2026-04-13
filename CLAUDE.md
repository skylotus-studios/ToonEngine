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
  main.cpp                  Entry point, GLFW window, game loop, FBO, shadow pass, render dispatch
  core/                     Low-level GPU abstractions
    mesh.h/.cpp             VAO/VBO/EBO, flexible vertex layouts (explicit location support)
    shader.h/.cpp           Compile/link from files, hot-reload via timestamps
    texture.h/.cpp          stb_image loading (file + memory), GL texture management
    material.h              Base color (vec4) + optional texture per sub-mesh
    transform.h/.cpp        Position/rotation/scale -> model matrix (TRS)
    animation.h             Joint, Skeleton, AnimationClip, keyframe types
    animator.h/.cpp         Playback, keyframe interpolation, joint matrix computation
  scene/                    Scene graph and asset loading
    scene.h                 Entity (meshes, skeleton, light, animator), Scene, Light types
    camera.h/.cpp           FPS fly camera (right-click + WASD)
    model_loader.h/.cpp     glTF + FBX: meshes, materials, skinning, skeleton, animations
  ui/                       Debug tooling
    overlay.h/.cpp          ImGui panels: render settings + entity inspector + anim controls
assets/shaders/
  model.vert                Shared vertex shader (MVP + GPU skinning)
  toon.frag                 Multi-light cel + CSM + specular + normal map + shadow tint + rim
  outline.*                 Inverted hull outlines (smooth normals, screen-space option)
  shadow.*                  Depth-only shadow map pass (with skinning)
  edge.*                    Sobel edge detection post-process (depth-based)
  triangle.*                Vertex-colored demo geometry
libs/
  cgltf/                    Single-header glTF 2.0 loader
  ufbx/                     Single-file FBX loader
```

## Render pipeline

1. **Shadow pass** -- 4-cascade CSM from the first directional light. Each cascade renders depth into a layer of a `GL_TEXTURE_2D_ARRAY` (2048x2048 per layer). Frustum split uses log-linear blend. Front-face culling to reduce acne. Supports skinned meshes.
2. **Scene pass** (to FBO if Sobel enabled, else to screen):
   - *VertexColor*: `triangle.*` shaders
   - *Toon*: front faces with `toon.frag` (multi-light cel + CSM + rim), back faces with `outline.*`
   - Light entities (directional/point, max 8) uploaded as uniform arrays
   - Skinned entities upload `uJoints[]` for GPU skinning
3. **Post-process** (optional) -- Sobel on linearized depth via `edge.*`
4. **ImGui overlay**

## Conventions

- Target-based CMake only. Dependencies via vcpkg manifest; single-header libs in `libs/`.
- `src/` is the include root. Cross-directory includes: `"core/mesh.h"`, `"scene/scene.h"`, etc.
- Shader hot-reload: edit `.vert`/`.frag` while running, changes apply next frame.
- Model loading via argv[1]. Auto-fit normalizes to 1 unit at origin.
- Per-mesh materials: base color + texture + normal map. glTF per-primitive PBR; FBX split by `material_parts`. Normal maps use derivative-based TBN (no tangent attribute needed).
- Vertex layout: non-skinned = pos+norm+uv+smoothNorm (stride 44), skinned = +boneIds+weights (stride 76). Smooth normals at location 5 for gap-free outlines.
- Skeletal animation: max 128 joints. Keyframe interpolation with hierarchy walk. Bind-pose local TRS as defaults for un-animated joints.
- Cascaded shadow maps: 4 cascades, 2048x2048 per cascade (`GL_TEXTURE_2D_ARRAY`), log-linear frustum split, `sampler2DArrayShadow` with 3x3 PCF, per-cascade bias scaling. Cascade selected by view-space depth. Shadowed fragments forced to shadow band.
- glTF textures loaded with flipY=false (matches glTF UV convention).
- Fixed timestep (60 Hz) for simulation, variable-rate rendering.

## Constraints

- **GL 4.1 only** -- macOS ceiling. No DSA, no compute, no SSBOs.
- **glTF + FBX only** -- no OBJ or other formats.
- Clang everywhere. MSVC and GCC are not tested.

## What's next

- Animation blending / crossfade between clips
- Scene serialization (save/load entity hierarchy)
- Ground plane / environment (sky, grid)
- Audio system
- Physics / collision detection
