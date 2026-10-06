# Style & task completion

C style (GNU, meson c_std=gnu17, warning_level=2):
- snake_case everywhere; statics prefixed s_ (file-local state), UPPERCASE macros; types R*/Game*/ChaseCam PascalCase-ish per existing headers.
- Comments: block comments explain *why*, no comment narration of obvious code. No docstrings.
- Single precision floats on purpose (Dreamcast SH-4): use 1.0f, sqrtf, expf; snprintf into stack `char buf[48]`.
- Opening brace same line for control flow; functions with brace on its line (match surrounding file).
- Core code never touches GL/platform headers — render.h/input.h only.

When a task is complete:
1. `meson compile -C build` must be warning-free; `meson test -C build` all green.
2. Update the pellet (pellets_comment with what shipped + code refs, pellets_close with reason). Note cross-issue interactions (blockers that are stale, downstream issues unblocked).
3. New game features get a GL-free test in tests/ + a meson test target in src/meson.build.
4. Temp/scratch in .scratch/ or /tmp/ only. Never commit unless asked (user committed 77a226e themselves once).
5. In-game verification by the user is the final acceptance for visual features.