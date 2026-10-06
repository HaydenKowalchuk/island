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

Build output goes to `.scratch/build_log.txt`; compiler warnings and errors still
go to the terminal. All transient logs and scratch files belong in `.scratch/`.

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

| Input      | Action                                   |
| ---------- | ---------------------------------------- |
| `W` / `S`  | throttle / brake                         |
| `A` / `D`  | steer                                    |
| `Shift`    | boost                                    |
| `Ctrl`     | slow                                     |
| `RMB` drag | look                                     |
| `E`        | context action (pickup / deliver / talk) |
| `R`        | reset camera                             |
| `Esc`      | quit                                     |

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

## Compile notes

- meson compile && (cd src && ./island)
- PSP (needs pspsdk at `/opt/toolchains/pspdev`):
  `meson setup build_psp --buildtype=release -Dplatform=psp --cross-file mips-allegrex-psp`
  then `meson compile -C build_psp`. Produces `build_psp/src/EBOOT.PBP`; assets
  are copied next to it and the loader also checks `host0:/assets` and
  `ms0:/PSP/GAME/island/assets`.
- PSP ISO (needs cdrtools, `brew install cdrtools`): `./scripts/build_psp_iso.sh`
  stages the UMD tree in `.scratch/psp_iso` from the skeleton in `tools/iso_root`
  and the XMB art in `assets_psp/` (icon0 144x80 + pic1 480x272), then writes
  `build_psp/island.iso` (BOOT.BIN/EBOOT.BIN from the build, textures under
  `PSP_GAME/USRDIR/assets`).
