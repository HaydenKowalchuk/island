/*
 * island
 *
 * OpenGL 1.2/1.5 fixed-function game scaffold.
 *
 * Controls:
 *   W/A/S/D   move        Q/E   down/up
 *   RMB drag  look        R     reset camera
 *   ESC       quit
 */

#include <stdio.h>
#include <stdlib.h>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include "camera.h"
#include "render.h"

#define WINDOW_W 1280
#define WINDOW_H 720
#define WINDOW_TITLE "island"

static GLFWwindow* g_window = NULL;
static FlyCam g_cam;

/* ---- Input ---- */
static bool g_rmb_down = false;
static double g_last_mouse_x = 0.0;
static double g_last_mouse_y = 0.0;

/* ---------------------------------------------------------------- */
/*  GLFW callbacks                                                   */
/* ---------------------------------------------------------------- */
static void error_callback(int error, const char* desc) {
    fprintf(stderr, "GLFW error %d: %s\n", error, desc);
}

static void key_callback(GLFWwindow* win, int key, int scancode, int action, int mods) {
    (void)scancode;
    (void)mods;
    if (action != GLFW_PRESS) return;

    if (key == GLFW_KEY_ESCAPE) {
        glfwSetWindowShouldClose(win, true);
    } else if (key == GLFW_KEY_R) {
        cam_reset(&g_cam);
    }
}

static void mouse_button_callback(GLFWwindow* win, int btn, int action, int mods) {
    (void)mods;
    if (btn != GLFW_MOUSE_BUTTON_RIGHT) return;

    if (action == GLFW_PRESS) {
        g_rmb_down = true;
        glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        glfwGetCursorPos(win, &g_last_mouse_x, &g_last_mouse_y);
    } else {
        g_rmb_down = false;
        glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    }
}

static void cursor_callback(GLFWwindow* win, double x, double y) {
    (void)win;
    if (!g_rmb_down) return;

    cam_rotate(&g_cam, (float)(x - g_last_mouse_x), (float)(y - g_last_mouse_y));
    g_last_mouse_x = x;
    g_last_mouse_y = y;
}

static void framebuffer_size_callback(GLFWwindow* win, int w, int h) {
    (void)win;
    if (w <= 0 || h <= 0) return;
    glViewport(0, 0, w, h);
}

/* ---------------------------------------------------------------- */
/*  Per-frame input polling                                          */
/* ---------------------------------------------------------------- */
static void handle_input(float dt) {
    int fwd = 0, side = 0, up = 0;

    if (glfwGetKey(g_window, GLFW_KEY_W) == GLFW_PRESS) fwd += 1;
    if (glfwGetKey(g_window, GLFW_KEY_S) == GLFW_PRESS) fwd -= 1;
    if (glfwGetKey(g_window, GLFW_KEY_D) == GLFW_PRESS) side += 1;
    if (glfwGetKey(g_window, GLFW_KEY_A) == GLFW_PRESS) side -= 1;
    if (glfwGetKey(g_window, GLFW_KEY_E) == GLFW_PRESS) up += 1;
    if (glfwGetKey(g_window, GLFW_KEY_Q) == GLFW_PRESS) up -= 1;

    if (glfwGetKey(g_window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS)
        g_cam.speed = 40.0f;
    else if (glfwGetKey(g_window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS)
        g_cam.speed = 3.0f;
    else
        g_cam.speed = 10.0f;

    cam_move(&g_cam, fwd, side, up, dt);
}

/* ---------------------------------------------------------------- */
/*  Context setup                                                    */
/* ---------------------------------------------------------------- */
static int create_window(void) {
    glfwSetErrorCallback(error_callback);
    if (!glfwInit()) {
        fprintf(stderr, "Failed to initialize GLFW\n");
        return -1;
    }

    /*
     * Request a legacy compatibility context so the fixed-function pipeline
     * (matrix stack, glBegin/glEnd, per-vertex lighting, vertex arrays) is
     * available. macOS caps this at 2.1, which is a superset of GL 1.5.
     */
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_ANY_PROFILE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    glfwWindowHint(GLFW_DOUBLEBUFFER, GLFW_TRUE);
    glfwWindowHint(GLFW_DEPTH_BITS, 24);

    g_window = glfwCreateWindow(WINDOW_W, WINDOW_H, WINDOW_TITLE, NULL, NULL);
    if (!g_window) {
        fprintf(stderr, "Failed to create GLFW window\n");
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(g_window);
    glfwSwapInterval(1);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        fprintf(stderr, "Failed to initialize GLAD\n");
        glfwDestroyWindow(g_window);
        glfwTerminate();
        return -1;
    }

    return 0;
}

static void report_gl_info(void) {
    printf("OpenGL version: %s\n", glGetString(GL_VERSION));
    printf("GLSL version:   %s\n", glGetString(GL_SHADING_LANGUAGE_VERSION));
    printf("vendor:         %s\n", glGetString(GL_VENDOR));
    printf("renderer:       %s\n", glGetString(GL_RENDERER));

#ifdef GL_CONTEXT_PROFILE_MASK
    GLint mask = 0;
    glGetIntegerv(GL_CONTEXT_PROFILE_MASK, &mask);
    if (mask & GL_CONTEXT_CORE_PROFILE_BIT)
        printf("profile:        core -- fixed-function calls will not work!\n");
    else
        printf("profile:        compatibility (fixed-function pipeline available)\n");
#endif
}

/* ---------------------------------------------------------------- */
/*  Main                                                             */
/* ---------------------------------------------------------------- */
int main(void) {
    if (create_window() != 0)
        return 1;

    report_gl_info();

    glfwSetKeyCallback(g_window, key_callback);
    glfwSetCursorPosCallback(g_window, cursor_callback);
    glfwSetMouseButtonCallback(g_window, mouse_button_callback);
    glfwSetFramebufferSizeCallback(g_window, framebuffer_size_callback);

    cam_init(&g_cam);
    render_init();

    double last_time = glfwGetTime();

    while (!glfwWindowShouldClose(g_window)) {
        double now = glfwGetTime();
        float dt = (float)(now - last_time);
        last_time = now;
        if (dt > 0.1f) dt = 0.1f;

        handle_input(dt);

        int fb_w, fb_h;
        glfwGetFramebufferSize(g_window, &fb_w, &fb_h);
        cam_update(&g_cam, fb_w, fb_h);

        render_begin_frame();
        render_set_projection((const float*)g_cam.proj);
        render_set_modelview((const float*)g_cam.view);
        render_ground();
        render_axes(4.0f);

        glfwSwapBuffers(g_window);
        glfwPollEvents();
    }

    render_shutdown();
    glfwDestroyWindow(g_window);
    glfwTerminate();
    return 0;
}