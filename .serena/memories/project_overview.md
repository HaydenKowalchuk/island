# island_1 overview

PS1-style low-poly delivery game: fly a hover-car between islands, pick up packages at a depot, deliver them to residents. C17, meson+ninja, OpenGL 1.2/1.5 fixed-function (GLAD, gl=2.1 compat) on desktop via GLFW; PSP/Dreamcast backends planned (KOS PVR, no GL).

Architecture (hard boundary):
- `src/core/` — platform-agnostic game: only talks to the RenderAPI (`render.h`) and input contract (`input.h`). Never includes GL/platform headers.
- `src/backend/{desktop,dreamcast,psp,common}/` — owns the platform API, consumes recorded frames.
- Recording API: game records primitives via `r_*` into `RenderFrame` (plain data: verts/draws, passes, composite classes OPAQUE/CUTOUT/TRANSLUCENT/OVERLAY); backend replays. `r_text_2d` = pixel coords from top-left, `r_rect_2d` = normalized [0,1] bottom-left, `hud_dialog_draw` = PS1 conversation textbox (`dialog.h`).
- Fixed tick + render split: `game_tick(dt)`, `game_render(frame_dt)`.

Key modules: game.c (state/quest/HUD emit), render.c, dialog.c (textbox + typewriter), input.c, camera/chase_cam/car, terrain/mesh/texture, font8x8.h.

Tests: GL-free unit tests in `tests/`, one meson test target each (quest, input, chase-cam, car, dialog, api-smoke, mode). Tests link explicit core sources or `core_sources`.

Task tracking: pellets (`pellets_list`/`ready`); AGENTS.md rules: temp files in /tmp/ or .scratch/.