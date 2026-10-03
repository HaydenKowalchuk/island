#ifndef ISLAND_BACKEND_DESKTOP_INPUT_GLFW_H
#define ISLAND_BACKEND_DESKTOP_INPUT_GLFW_H

#include <GLFW/glfw3.h>

/* Registers GLFW callbacks that inject events into the core input contract. */
void input_glfw_init(GLFWwindow* window);

#endif
