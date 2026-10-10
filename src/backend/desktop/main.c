/*
 * island -- desktop backend
 *
 * Owns the window and the main loop. Each frame it polls input into the core
 * contract, advances the core simulation on a fixed timestep, asks core to
 * record a frame through the RenderAPI, then presents that frame with GL.
 *
 * Controls:
 *   W/S   throttle / brake       A/D or arrows  steer
 *   SHIFT boost                  E     interact          F     enter/exit car
 *   RMB drag  look               C     dev fly cam       F3    debug stats
 *   ESC       quit                R     dev reset
 *   Menu (before play): UP/DOWN or W/S move, ENTER/SPACE or W start,
 *   BACKSPACE back; any key skips the intro.
 */

#include <GLFW/glfw3.h>
#include <glad/glad.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio.h"
#include "game.h"
#include "input.h"
#include "input_glfw.h"
#include "menu.h"
#include "render_gl.h"

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

static void error_callback(int error, const char* desc) {
  fprintf(stderr, "GLFW error %d: %s\n", error, desc);
}

static void framebuffer_size_callback(GLFWwindow* win, int w, int h) {
  (void)win;
  if (w <= 0 || h <= 0)
    return;
  glViewport(0, 0, w, h);
  game_resize(w, h);
}

static void report_gl_info(void) {
  printf("OpenGL version: %s\n", glGetString(GL_VERSION));
  printf("GLSL version:   %s\n", glGetString(GL_SHADING_LANGUAGE_VERSION));
  printf("vendor:         %s\n", glGetString(GL_VENDOR));
  printf("renderer:       %s\n", glGetString(GL_RENDERER));

#ifdef GL_CONTEXT_PROFILE_MASK
  GLint mask = 0;
  glGetIntegerv(GL_CONTEXT_PROFILE_MASK, &mask);
  if (mask & GL_CORE_PROFILE_BIT)
    printf("profile:        core -- fixed-function calls will not work!\n");
  else
    printf("profile:        compatibility (fixed-function pipeline available)\n");
#endif

  fflush(stdout);
}

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

int main(int argc, char** argv) {
  if (create_window() != 0)
    return 1;

  report_gl_info();

  glfwSetFramebufferSizeCallback(g_window, framebuffer_size_callback);

  input_init();
  input_glfw_init(g_window);

  int fb_w = WINDOW_W, fb_h = WINDOW_H;
  glfwGetFramebufferSize(g_window, &fb_w, &fb_h);
  glViewport(0, 0, fb_w, fb_h);
  game_resize(fb_w, fb_h);

  /* render_init installs the texture loader, so content built in game_init
   * can request textures and have them uploaded immediately. */
  render_init();
  /* Audio before game_init so the first menu/game ticks are never silent;
   * a missing device logs and stays silent (island1-a9pu). */
  audio_init();
  game_init();

  /* Hide the gameplay HUD with --no-hud or ISLAND_NO_HUD (screenshots, art). */
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--no-hud") == 0)
      game_set_hud(0);
  }
  if (getenv("ISLAND_NO_HUD"))
    game_set_hud(0);

  double last_time = glfwGetTime();
  float accumulator = 0.0f;

  while (!glfwWindowShouldClose(g_window)) {
    double now = glfwGetTime();
    float frame_dt = (float)(now - last_time);
    last_time = now;

    if (frame_dt > MAX_FRAME_DT)
      frame_dt = MAX_FRAME_DT;

    glfwPollEvents();
    backend_input_poll();

    /* ESC goes through the input manager like everything else, but a screen
     * that consumes menu-back (pause, inventory, controls) owns the press —
     * it must resume/go back, not close the window (island1-8nfp). */
    if (input_held(ACT_QUIT) && !menu_consumes_back())
      glfwSetWindowShouldClose(g_window, true);

    accumulator += frame_dt;

    int ticks = 0;
    while (accumulator >= TICK && ticks < MAX_TICKS) {
      input_begin_frame();

      game_tick(TICK);

      accumulator -= TICK;
      ticks++;
    }

    if (ticks == MAX_TICKS)
      accumulator = 0.0f; /* Drop the backlog rather than spiral. */

    glfwGetFramebufferSize(g_window, &fb_w, &fb_h);

    game_render(frame_dt);
    render_present(fb_w, fb_h);

    glfwSwapBuffers(g_window);
  }

  game_shutdown();
  audio_shutdown();
  render_shutdown();
  glfwDestroyWindow(g_window);
  glfwTerminate();
  return 0;
}
