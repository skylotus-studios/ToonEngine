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
  model_loader.h/.cpp  glTF (cgltf) and FBX (ufbx) loading with auto-normals
assets/shaders/
  triangle.*         Vertex-colored demo geometry
  model.vert         Shared vertex shader for loaded models (MVP + normal transform)
  toon.frag          Cel shading: three-band quantized diffuse
  outline.*          Inverted hull silhouette outlines
  model.frag         Plain diffuse lighting (non-toon fallback)
libs/
  cgltf/             Single-header glTF 2.0 loader
  ufbx/              Single-file FBX loader
```

### Render pipeline

1. Clear color + depth
2. Demo triangles: `triangle.vert` + `triangle.frag` (vertex colors)
3. Loaded model pass 1 (front faces): `model.vert` + `toon.frag` (cel shading + materials)
4. Loaded model pass 2 (back faces): `outline.vert` + `outline.frag` (inverted hull outlines)

Face culling is enabled only during the model passes.

### Conventions

- Modern target-based CMake only. No global `include_directories` or `link_libraries`.
- All dependencies via vcpkg manifest except single-header/file libs which go in `libs/`.
- Shader hot-reload: edit any `.vert`/`.frag` while running, changes apply next frame.
- Model loading: pass file path as argv[1]. Auto-fit normalizes to ~5 units at origin.
- Vertex layout for loaded models: position(vec3) + normal(vec3) + texcoord(vec2), stride 32.
- Fixed timestep (60Hz) for simulation, variable-rate rendering. Camera input at frame rate.

## Constraints

- **GL 4.1 only** -- macOS ceiling. No 4.3+ features (no debug callbacks on Mac, no DSA, no compute).
- **glTF + FBX only** -- no OBJ or other formats.
- Clang everywhere. MSVC and GCC are not tested.

## What's next

- ImGui debug overlay (vcpkg: `imgui[glfw-binding,opengl3-binding]`)
- Scene/entity list (replace hardcoded globals)
- Per-mesh material textures from glTF/FBX (partially implemented)
- Toon shader refinements: rim lighting, shadow color ramp, Sobel edge detection
- Skeletal animation
