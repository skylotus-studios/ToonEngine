cmake_minimum_required(VERSION 3.21)

project(ToonEngine
    VERSION 0.1.0
    DESCRIPTION "ToonEngine - game engine"
    LANGUAGES CXX
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

find_package(glfw3 CONFIG REQUIRED)
find_package(glad  CONFIG REQUIRED)
find_package(glm   CONFIG REQUIRED)

add_executable(ToonEngine
    src/main.cpp
    src/shader.h
    src/shader.cpp
    src/camera.h
    src/camera.cpp
)

target_compile_features(ToonEngine PRIVATE cxx_std_20)

set_target_properties(ToonEngine PROPERTIES
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS        OFF
)

target_link_libraries(ToonEngine
    PRIVATE
        glfw
        glad::glad
        glm::glm
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
