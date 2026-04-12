#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include "camera.h"
#include "mesh.h"
#include "shader.h"
#include "texture.h"
#include "transform.h"

#include <glm/gtc/type_ptr.hpp>

#include <chrono>
#include <cstdio>
#include <cstdlib>

namespace {

    constexpr int    kWindowWidth = 3840;
    constexpr int    kWindowHeight = 2160;
    constexpr char   kWindowTitle[] = "ToonEngine";
    constexpr double kFixedTimestep = 1.0 / 60.0;

    ShaderProgram gShader{};
    Mesh          gTriangleMesh{};
    Texture       gDefaultTexture{};

    Camera gCamera{};
    int    gFramebufferW = kWindowWidth;
    int    gFramebufferH = kWindowHeight;

    double gLastMouseX = 0.0;
    double gLastMouseY = 0.0;
    bool   gRightMouseHeld = false;

    // clang-format off
    Transform gTransforms[] = {
        { { 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f,   0.0f}, {1.0f, 1.0f, 1.0f} },
        { { 2.0f, 0.0f, 0.0f}, {0.0f, 0.0f,  45.0f}, {0.8f, 0.8f, 0.8f} },
        { {-2.0f, 0.5f, 0.0f}, {0.0f, 0.0f, -30.0f}, {1.2f, 1.2f, 1.2f} },
    };
    // clang-format on

    constexpr int kTransformCount = sizeof(gTransforms) / sizeof(gTransforms[0]);

    void GlfwErrorCallback(int error, const char* description) {
        std::fprintf(stderr, "[GLFW] error %d: %s\n", error, description);
    }

    void FramebufferSizeCallback(GLFWwindow*, int width, int height) {
        gFramebufferW = width;
        gFramebufferH = height;
        glViewport(0, 0, width, height);
    }

    void MouseButtonCallback(GLFWwindow* window, int button, int action, int /*mods*/) {
        if (button == GLFW_MOUSE_BUTTON_RIGHT) {
            gRightMouseHeld = (action == GLFW_PRESS);
            if (gRightMouseHeld) {
                glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                glfwGetCursorPos(window, &gLastMouseX, &gLastMouseY);
            } else {
                glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            }
        }
    }

    void CursorPosCallback(GLFWwindow*, double x, double y) {
        if (!gRightMouseHeld) return;
        auto dx = static_cast<float>(x - gLastMouseX);
        auto dy = static_cast<float>(gLastMouseY - y);
        gLastMouseX = x;
        gLastMouseY = y;
        CameraProcessMouse(gCamera, dx, dy);
    }

    void APIENTRY GlDebugCallback(GLenum source, GLenum type, GLuint id,
        GLenum severity, GLsizei /*length*/,
        const GLchar* message, const void* /*user*/) {
        if (severity == GL_DEBUG_SEVERITY_NOTIFICATION) return;

        const char* src = "?";
        switch (source) {
        case GL_DEBUG_SOURCE_API:             src = "API";        break;
        case GL_DEBUG_SOURCE_WINDOW_SYSTEM:   src = "WINDOW";     break;
        case GL_DEBUG_SOURCE_SHADER_COMPILER: src = "SHADER";     break;
        case GL_DEBUG_SOURCE_THIRD_PARTY:     src = "THIRDPARTY"; break;
        case GL_DEBUG_SOURCE_APPLICATION:     src = "APP";        break;
        case GL_DEBUG_SOURCE_OTHER:           src = "OTHER";      break;
        }
        const char* typ = "?";
        switch (type) {
        case GL_DEBUG_TYPE_ERROR:               typ = "ERROR";       break;
        case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR: typ = "DEPRECATED";  break;
        case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR:  typ = "UB";          break;
        case GL_DEBUG_TYPE_PORTABILITY:         typ = "PORTABILITY"; break;
        case GL_DEBUG_TYPE_PERFORMANCE:         typ = "PERF";        break;
        case GL_DEBUG_TYPE_MARKER:              typ = "MARKER";      break;
        case GL_DEBUG_TYPE_OTHER:               typ = "OTHER";       break;
        }
        const char* sev = "?";
        switch (severity) {
        case GL_DEBUG_SEVERITY_HIGH:   sev = "HIGH";   break;
        case GL_DEBUG_SEVERITY_MEDIUM: sev = "MEDIUM"; break;
        case GL_DEBUG_SEVERITY_LOW:    sev = "LOW";    break;
        }
        std::fprintf(stderr, "[GL %s/%s/%s #%u] %s\n", sev, typ, src, id, message);
    }

    void Update(double dt) {
        gTransforms[1].rotation.z += 45.0f * static_cast<float>(dt);
    }

    void Render(double /*alpha*/) {
        glClearColor(0.08f, 0.09f, 0.11f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glUseProgram(gShader.id);
        BindTexture(gDefaultTexture, 0);
        glUniform1i(glGetUniformLocation(gShader.id, "uTexture"), 0);

        float aspect = (gFramebufferH > 0)
            ? static_cast<float>(gFramebufferW) / static_cast<float>(gFramebufferH)
            : 1.0f;
        glm::mat4 vp = CameraProjectionMatrix(gCamera, aspect)
                      * CameraViewMatrix(gCamera);

        GLint mvpLoc = glGetUniformLocation(gShader.id, "uMVP");
        for (int i = 0; i < kTransformCount; ++i) {
            glm::mat4 mvp = vp * TransformMatrix(gTransforms[i]);
            glUniformMatrix4fv(mvpLoc, 1, GL_FALSE, glm::value_ptr(mvp));
            DrawMesh(gTriangleMesh);
        }
    }

} // namespace

int main() {
    glfwSetErrorCallback(GlfwErrorCallback);

    if (!glfwInit()) {
        std::fprintf(stderr, "glfwInit failed\n");
        return EXIT_FAILURE;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    // Required on macOS; harmless on Windows.
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#ifndef NDEBUG
    glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);
#endif

    GLFWwindow* window = glfwCreateWindow(
        kWindowWidth, kWindowHeight, kWindowTitle, nullptr, nullptr);
    if (!window) {
        std::fprintf(stderr, "glfwCreateWindow failed\n");
        glfwTerminate();
        return EXIT_FAILURE;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) {
        std::fprintf(stderr, "gladLoadGLLoader failed\n");
        glfwDestroyWindow(window);
        glfwTerminate();
        return EXIT_FAILURE;
    }

    std::printf("GL vendor:   %s\n", reinterpret_cast<const char*>(glGetString(GL_VENDOR)));
    std::printf("GL renderer: %s\n", reinterpret_cast<const char*>(glGetString(GL_RENDERER)));
    std::printf("GL version:  %s\n", reinterpret_cast<const char*>(glGetString(GL_VERSION)));
    std::printf("GLSL:        %s\n", reinterpret_cast<const char*>(glGetString(GL_SHADING_LANGUAGE_VERSION)));

    // KHR_debug: core in GL 4.3+, extension below that. macOS 4.1 won't have it.
    if (glDebugMessageCallback) {
        glEnable(GL_DEBUG_OUTPUT);
        glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
        glDebugMessageCallback(GlDebugCallback, nullptr);
        glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, nullptr, GL_TRUE);
    }

    glfwSetFramebufferSizeCallback(window, FramebufferSizeCallback);
    glfwSetMouseButtonCallback(window, MouseButtonCallback);
    glfwSetCursorPosCallback(window, CursorPosCallback);
    {
        int fbw = 0, fbh = 0;
        glfwGetFramebufferSize(window, &fbw, &fbh);
        gFramebufferW = fbw;
        gFramebufferH = fbh;
        glViewport(0, 0, fbw, fbh);
    }

    glEnable(GL_DEPTH_TEST);

    if (!LoadShader(gShader, "assets/shaders/triangle.vert",
                             "assets/shaders/triangle.frag")) {
        std::fprintf(stderr, "Failed to load shaders\n");
        glfwDestroyWindow(window);
        glfwTerminate();
        return EXIT_FAILURE;
    }

    gDefaultTexture = CreateWhiteTexture();

    // clang-format off
    float vertices[] = {
    //  position              color              texcoord
        -0.5f, -0.5f, 0.0f,  1.0f, 0.0f, 0.0f,  0.0f, 0.0f,
         0.5f, -0.5f, 0.0f,  0.0f, 1.0f, 0.0f,  1.0f, 0.0f,
         0.0f,  0.5f, 0.0f,  0.0f, 0.0f, 1.0f,  0.5f, 1.0f,
    };
    VertexAttrib attribs[] = {
        {3, GL_FLOAT, 0},
        {3, GL_FLOAT, 3 * sizeof(float)},
        {2, GL_FLOAT, 6 * sizeof(float)},
    };
    // clang-format on

    gTriangleMesh = CreateMesh(
        vertices, sizeof(vertices), 8 * sizeof(float),
        attribs, 3, 3);

    using Clock = std::chrono::high_resolution_clock;
    auto   prev = Clock::now();
    double accumulator = 0.0;

    while (!glfwWindowShouldClose(window)) {
        const auto   now = Clock::now();
        const double frame = std::chrono::duration<double>(now - prev).count();
        prev = now;

        // Clamp to avoid spiral-of-death after hitches / breakpoints.
        accumulator += (frame > 0.25 ? 0.25 : frame);

        while (accumulator >= kFixedTimestep) {
            Update(kFixedTimestep);
            accumulator -= kFixedTimestep;
        }

        if (gRightMouseHeld)
            CameraProcessKeyboard(gCamera, window, static_cast<float>(frame));

        ReloadIfChanged(gShader);

        const double alpha = accumulator / kFixedTimestep;
        Render(alpha);

        glfwSwapBuffers(window);
        glfwPollEvents();

        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }
    }

    DestroyMesh(gTriangleMesh);
    DestroyTexture(gDefaultTexture);
    DestroyShader(gShader);

    glfwDestroyWindow(window);
    glfwTerminate();
    return EXIT_SUCCESS;
}
