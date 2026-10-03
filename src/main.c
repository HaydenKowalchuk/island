/*
 * island
 *
 * OpenGL 1.2/1.5 fixed-function game scaffold.
 *
 * Controls:
 *   W/S   throttle / brake       A/D  steer
 *   SHIFT boost                  CTRL  slow       R     reset camera
 *   RMB drag  look                E     context action
 *   ESC       quit
 */

#include <stdio.h>
#include <stdlib.h>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include "camera.h"
#include "input.h"
#include "render.h"

#define WINDOW_W 1280
#define WINDOW_H 720
#define WINDOW_TITLE "island"

/* Fixed logic tick. Physics and game rules run at exactly this rate. */
#define TICK_HZ 60.0f
#define TICK (1.0f / TICK_HZ)

/* Cap on catch-up ticks per frame; beyond this the backlog is dropped. */
#define MAX_TICKS 5

/* Longest frame delta we accept, to survive window drags and breakpoints. */
#define MAX_FRAME_DT 0.25f

static GLFWwindow* g_window = NULL;
static FlyCam g_cam;

/* Simulation clock, advanced only in whole ticks. */
static uint64_t g_tick_count = 0;
static float g_time = 0.0f;

/* Leftover time between ticks, 0..1. Feeds render interpolation. */
static float g_render_alpha = 0.0f;

/* ---------------------------------------------------------------- */
/*  GLFW callbacks                                                   */
/* ---------------------------------------------------------------- */
static void error_callback(int error, const char* desc) {
    fprintf(stderr, "GLFW error %d: %s\n", error, desc);
}

static void framebuffer_size_callback(GLFWwindow* win, int w, int h) {
    (void)win;
    if (w <= 0 || h <= 0) return;
    glViewport(0, 0, w, h);
}

/* ---------------------------------------------------------------- */
/*  Fixed-tick simulation                                            */
/* ---------------------------------------------------------------- */
static void step_once(float dt) {
    const InputState* in = input_state();

    /* Look and speed adjustments belong to the sim so they stay in step. */
    if (input_held(ACT_LOOK))
        cam_rotate(&g_cam, in->mouse_dx, in->mouse_dy);

    if (input_held(ACT_BOOST))
        g_cam.speed = 40.0f;
    else if (input_held(ACT_SLOW))
        g_cam.speed = 3.0f;
    else
        g_cam.speed = 10.0f;

    if (input_pressed(ACT_RESET))
        cam_reset(&g_cam);

    cam_move(&g_cam, (int)input_axis_throttle(), (int)input_axis_steer(), 0, dt);

    g_time += dt;
    g_tick_count++;
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

    glfwSetFramebufferSizeCallback(g_window, framebuffer_size_callback);
    input_init();

    cam_init(&g_cam);
    render_init();

    double last_time = glfwGetTime();
    float accumulator = 0.0f;

    while (!glfwWindowShouldClose(g_window)) {
        double now = glfwGetTime();
        float frame_dt = (float)(now - last_time);
        last_time = now;

        if (frame_dt > MAX_FRAME_DT)
            frame_dt = MAX_FRAME_DT;

        glfwPollEvents();

        accumulator += frame_dt;

        /* ESC goes through the input manager like everything else. */
        if (input_held(ACT_QUIT))
            glfwSetWindowShouldClose(g_window, true);

        int ticks = 0;
        while (accumulator >= TICK && ticks < MAX_TICKS) {
            input_begin_frame();
            input_poll();

            step_once(TICK);

            accumulator -= TICK;
            ticks++;
        }

        if (ticks == MAX_TICKS)
            accumulator = 0.0f; /* Drop the backlog rather than spiral. */

        /*
         * Render with the leftover time as an interpolation alpha, so rendering
         * still happens on frames where no tick ran.
         */
        g_render_alpha = accumulator / TICK;
        int fb_w, fb_h;
        glfwGetFramebufferSize(g_window, &fb_w, &fb_h);
        cam_update(&g_cam, fb_w, fb_h);

        render_set_projection((const float*)g_cam.proj);
        render_set_modelview((const float*)g_cam.view);
        render_frame();

        glfwSwapBuffers(g_window);
    }

    render_shutdown();
    glfwDestroyWindow(g_window);
    glfwTerminate();
    return 0;
}