// ImGui debug overlay for runtime parameter tweaking.
//
// RenderSettings holds all tunable rendering parameters that were previously
// hardcoded in shaders or main.cpp. The overlay draws an ImGui panel each
// frame so you can adjust them live. The entity list lets you inspect and
// edit transforms for every object in the scene.

#pragma once

#include "scene/scene.h"

#include <glm/glm.hpp>

struct GLFWwindow;

struct RenderSettings {
    // Toon shading (lights are now per-entity, not here).
    float bandThresholdHigh = 0.5f;
    float bandThresholdLow  = 0.0f;
    float brightIntensity   = 1.0f;
    float midIntensity      = 0.55f;
    float shadowIntensity   = 0.15f;

    // Shadow color ramp — multiplied into shadow/mid bands.
    glm::vec3 shadowTint{1.0f, 1.0f, 1.0f};

    // Rim lighting.
    glm::vec3 rimColor{1.0f, 1.0f, 1.0f};
    float     rimPower    = 3.0f;
    float     rimStrength = 0.4f;

    // Outlines (inverted hull).
    float     outlineWidth = 0.755f;
    glm::vec4 outlineColor{0.05f, 0.05f, 0.05f, 1.0f};
    bool      outlineScreenSpace = false;

    // Sobel edge detection (post-process).
    bool      edgeEnabled   = true;
    glm::vec3 edgeColor{0.0f, 0.0f, 0.0f};
    float     edgeThreshold = 0.02f;
    float     edgeWidth     = 1.0f;

    // Scene.
    glm::vec3 clearColor{0.08f, 0.09f, 0.11f};

    // Debug.
    bool debugDisableSkinning = false;
};

// Initialize ImGui for the given GLFW/OpenGL context. Call once after
// the GL context is current and GLAD is loaded.
void OverlayInit(GLFWwindow* window);

// Start a new ImGui frame. Call once per frame before OverlayRender.
void OverlayNewFrame();

// Draw the debug panel and submit ImGui draw data. Returns true if
// ImGui wants to capture mouse/keyboard input (i.e. the user is
// interacting with a widget — the camera should ignore input).
bool OverlayRender(RenderSettings& settings, Scene& scene, float fps);

// Shutdown ImGui. Call once before destroying the GL context.
void OverlayShutdown();
