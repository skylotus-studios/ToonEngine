# ToonEngine

From-scratch OpenGL 4.1 game engine focused on stylized/toon rendering.

## Build

- **Toolchain**: Clang (clang-cl on Windows, Apple Clang on macOS)
- **Build system**: CMake 3.21+ with Presets (`cmake --preset windows-debug`)
- **Package manager**: vcpkg manifest mode (`vcpkg.json`). Single-header libs go in `libs/`
- **Generator**: Ninja
- **Standard**: C++20, no extensions

```
cmake --preset windows-debug
cmake --build --preset windows-debug
./build/windows-debug/ToonEngine.exe [model_path]
```

## Architecture

OpenGL 4.1 core profile (macOS ceiling). No DSA, no compute, no SSBOs.

### Source layout

```
src/
  main.cpp           Entry point, GLFW window, game loop, render passes
  shader.h/.cpp      Compile/link shaders from files, hot-reload via timestamps
  camera.h/.cpp      FPS fly camera (right-click + WASD)
  mesh.h/.cpp        VAO/VBO/EBO abstraction, flexible vertex layouts
  transform.h/.cpp   Position/rotation/scale -> model matrix (TRS order)
  texture.h/.cpp     stb_image loading, from-file and from-memory
  material.h         Base color + texture per mesh
  model_loader.h/.cpp  glTF (cgltf) and FBX (ufbx) loading, per-material splitting, auto-normals
  scene.h            Entity (name + transform + submeshes + shading mode) and Scene
  overlay.h/.cpp     ImGui debug overlay: render settings panel + entity list/inspector
assets/shaders/
  triangle.*         Vertex-colored demo geometry
  model.vert         Shared vertex shader (MVP + world pos/normal for rim + toon)
  toon.frag          Cel shading + shadow tint + rim lighting (all uniform-driven)
  outline.*          Inverted hull silhouette outlines
  edge.*             Sobel edge detection post-process (depth-based, fullscreen pass)
  model.frag         Plain diffuse lighting (non-toon fallback)
libs/
  cgltf/             Single-header glTF 2.0 loader
  ufbx/              Single-file FBX loader
```

### Render pipeline

The render loop iterates `Scene::entities` and dispatches by `ShadingMode`.
When Sobel edge detection is enabled, the scene renders to an FBO first;
otherwise it goes straight to the default framebuffer.

1. **Scene pass** (to FBO or screen):
   - **VertexColor** entities: `triangle.vert` + `triangle.frag` (vertex colors)
   - **Toon** entities (two sub-passes):
     1. Front faces: `model.vert` + `toon.frag` (cel shading + shadow tint + rim)
     2. Back faces: `outline.vert` + `outline.frag` (inverted hull outlines)
   - Face culling is enabled only during the Toon sub-passes.
2. **Post-process pass** (if edge detection enabled):
   - Fullscreen triangle (attributeless, gl_VertexID) reads FBO color + depth
   - `edge.vert` + `edge.frag`: Sobel operator on linearized depth, smoothstep threshold
   - Composites edges over scene color to the default framebuffer
3. **ImGui overlay** on top of final image

### ImGui overlay

Two panels rendered after the scene each frame:

- **ToonEngine** — FPS counter, toon shading (light dir, band thresholds,
  intensities, shadow tint), rim lighting (color, power, strength), inverted
  hull outlines (width, color), Sobel edge detection (enable, color, threshold,
  width), background color. All live-tweakable.
- **Entities** — selectable list of all entities in the scene. Selecting one
  shows an inspector with shading mode, mesh/triangle counts, and editable
  position/rotation/scale. Camera input is suppressed while interacting with
  ImGui widgets.

### Conventions

- Modern target-based CMake only. No global `include_directories` or `link_libraries`.
- All dependencies via vcpkg manifest except single-header/file libs which go in `libs/`.
- Shader hot-reload: edit any `.vert`/`.frag` while running, changes apply next frame.
- Model loading: pass file path as argv[1]. Auto-fit normalizes to ~5 units at origin.
- Per-mesh materials: glTF extracts per-primitive PBR base color + texture (file & embedded).
  FBX meshes are split by `material_parts` so each material partition gets its own SubMesh.
  Texture path resolution tries relative path, then absolute, then filename-only fallback.
- Vertex layout for loaded models: position(vec3) + normal(vec3) + texcoord(vec2), stride 32.
- Fixed timestep (60Hz) for simulation, variable-rate rendering. Camera input at frame rate.

## Constraints

- **GL 4.1 only** -- macOS ceiling. No 4.3+ features (no debug callbacks on Mac, no DSA, no compute).
- **glTF + FBX only** -- no OBJ or other formats.
- Clang everywhere. MSVC and GCC are not tested.

## What's next

- Skeletal animation
- Point / directional light entities (replace hardcoded light direction)
- Shadow mapping
- Normal-based edge detection (Sobel on normals, complementing depth edges)
- Specular highlight band (toon-style specular lobe)
- Normal map support (sample normal texture in toon.frag)
