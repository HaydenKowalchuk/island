# island

PS1-style low-poly game: deliver packages to island inhabitants with a floating
car that skates above water.

## Requirements

- macOS with Xcode command line tools
- [meson](https://mesonbuild.com) and ninja

GLFW and cglm are pulled in automatically as meson wrap subprojects, so nothing
needs to be installed by hand. GLAD (OpenGL `gl=2.1`, compatibility profile) is
vendored under `subprojects/glad`.

## Build

```sh
./run.sh
```

Build output goes to `build_log.txt`; compiler warnings and errors still go to
the terminal.

Manual equivalent:

```sh
meson setup build
meson compile -C build
./build/src/island
```

Reconfigure from scratch:

```sh
rm -rf build && meson setup build
```

## Rendering

The target is the OpenGL 1.2/1.5 fixed-function pipeline. `main.c` requests a
legacy compatibility context (`GLFW_CONTEXT_VERSION_MAJOR=2`,
`GLFW_CONTEXT_VERSION_MINOR=1`, `GLFW_OPENGL_ANY_PROFILE`); on macOS the legacy
context is 2.1, a superset of GL 1.5.

That means: matrix stack via `glMatrixMode`/`glLoadMatrixf`, `glBegin`/`glEnd`
immediate mode and client-state vertex arrays (`glVertexPointer`,
`glColorPointer`, `glTexCoordPointer`, `glDrawArrays`). No VAOs, no VBOs, no
shaders.

## Controls

| Input | Action |
| --- | --- |
| `W` / `S` | throttle / brake-reverse |
| `A` / `D` | steer |
| `Q` / `E` | down / up |
| `Shift` | boost | 
| `Ctrl` | slow |
| RMB drag | look |
| `R` | reset camera |
| `Esc` | quit |

## Layout

```
src/
  main.c      window, context, main loop
  camera.c/h  fly camera (yaw/pitch, cglm matrices)
  input.c/h   action-based input with edge detection
  gfx.c/h     fixed-function state manager
  passes.c/h  render passes (sky, ocean, terrain, ...)
  render.c/h  pass implementations
subprojects/
  glad/       vendored GL loader
```

## Task tracking

Work is tracked as pellets issues stored in `.pellets/`. `ready` lists the
unblocked tasks.