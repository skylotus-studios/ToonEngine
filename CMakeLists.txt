cmake_minimum_required(VERSION 3.21)

project(ToonEngine
    VERSION 0.1.0
    DESCRIPTION "ToonEngine - game engine"
    LANGUAGES C CXX
)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR
        "In-source builds are disabled. Use one of the CMake presets "
        "(e.g. 'cmake --preset windows-debug').")
endif()

if(DEFINED VCPKG_TARGET_TRIPLET)
    set(_toonengine_vcpkg_prefix_candidates)

    if(DEFINED VCPKG_INSTALLED_DIR)
        list(APPEND _toonengine_vcpkg_prefix_candidates
            "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}"
        )
    endif()

    list(APPEND _toonengine_vcpkg_prefix_candidates
        "${CMAKE_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}"
    )

    foreach(_toonengine_vcpkg_prefix IN LISTS _toonengine_vcpkg_prefix_candidates)
        if(EXISTS "${_toonengine_vcpkg_prefix}")
            list(PREPEND CMAKE_PREFIX_PATH "${_toonengine_vcpkg_prefix}")
        endif()
    endforeach()

    list(REMOVE_DUPLICATES CMAKE_PREFIX_PATH)
endif()

# --- Dependencies (vcpkg) ---------------------------------------------------
find_package(glfw3 CONFIG REQUIRED)
find_package(glad  CONFIG REQUIRED)
find_package(glm   CONFIG REQUIRED)
find_package(Stb REQUIRED)
find_package(imgui CONFIG REQUIRED)

# --- Vendored libraries (single-header / single-file, committed in libs/) ---
# ufbx: compiled as a static lib so its C code gets its own warning settings.
add_library(ufbx STATIC libs/ufbx/ufbx.c)
target_include_directories(ufbx SYSTEM PUBLIC libs/ufbx)
if(MSVC)
    target_compile_options(ufbx PRIVATE /W0)
else()
    target_compile_options(ufbx PRIVATE -w)
endif()

# --- Engine executable -------------------------------------------------------
add_executable(ToonEngine
    src/main.cpp
    src/core/shader.h
    src/core/shader.cpp
    src/core/mesh.h
    src/core/mesh.cpp
    src/core/transform.h
    src/core/transform.cpp
    src/core/texture.h
    src/core/texture.cpp
    src/core/material.h
    src/core/animation.h
    src/core/animator.h
    src/core/animator.cpp
    src/scene/camera.h
    src/scene/camera.cpp
    src/scene/model_loader.h
    src/scene/model_loader.cpp
    src/scene/scene.h
    src/scene/serializer.h
    src/scene/serializer.cpp
    src/ui/overlay.h
    src/ui/overlay.cpp
    src/ui/themes.h
    src/ui/themes.cpp
    libs/imguizmo/ImGuizmo.h
    libs/imguizmo/ImGuizmo.cpp
)

target_compile_features(ToonEngine PRIVATE cxx_std_20)

set_target_properties(ToonEngine PROPERTIES
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS        OFF
)

target_include_directories(ToonEngine PRIVATE
    src
    ${Stb_INCLUDE_DIR}
    libs/cgltf
    libs/imguizmo
)

target_link_libraries(ToonEngine
    PRIVATE
        glfw
        glad::glad
        glm::glm
        imgui::imgui
        ufbx
        $<$<PLATFORM_ID:Windows>:dwmapi>
)

if(MSVC)
    # clang-cl on Windows understands MSVC-style flags.
    target_compile_options(ToonEngine PRIVATE /W4 /permissive-)
else()
    target_compile_options(ToonEngine PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wnon-virtual-dtor
        -Wold-style-cast
        -Woverloaded-virtual
        -Wnull-dereference
        -Wdouble-promotion
        -Wformat=2
    )
endif()

if(APPLE)
    target_link_libraries(ToonEngine PRIVATE
        "-framework OpenGL"
        "-framework Cocoa"
        "-framework IOKit"
        "-framework CoreVideo"
        "-framework CoreFoundation"
    )
endif()
