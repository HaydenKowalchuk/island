#ifndef ISLAND_CORE_DIALOG_H
#define ISLAND_CORE_DIALOG_H

/*
 * PS1-style conversation textbox (island1-a2xf, overflow fix island1-s81y).
 *
 * Two layers:
 *
 *   hud_dialog_draw()  pure draw primitive: bottom-third black box with a
 *                      2px white border, name label top-left, body text below,
 *                      revealed up to `type_progress` characters. Drawn through
 *                      the recorded R_PASS_HUD pass (screen-space, OVERLAY
 *                      composite, lighting/depth off by construction).
 *
 *   dialog_*()         a tiny conversation player that owns the typewriter
 *                      (1 char / 30 ms) and the E rule: the first E completes
 *                      the current line instantly, the next E pages down if
 *                      the wrapped line has more rows than the box shows,
 *                      then advances, and after the last line closes the box.
 *                      island1-9fm3 (NPC conversation system) feeds lines in
 *                      through dialog_start().
 *
 * Layout: the text scale (2x, else 1x) is picked per viewport so the longest
 * line of the conversation fits the box when possible (PSP 480x272 runs at
 * 1x). Lines that still wrap past the visible rows - e.g. below ~240px wide
 * - are paged with E (blinking down triangle marks more rows), so the whole
 * message is always reachable; nothing is silently clipped.
 */

/* Longest wrapped row the draw buffer holds; also the staging buffer size. */
#define DIALOG_MAX_CHARS 96
/* Lines one conversation may hold (matches the Dialogue tables in 9fm3). */
#define DIALOG_MAX_LINES 4

/*
 * Draw the textbox page `page` (0-based; see dialog_page()). `name` may be
 * NULL for an anonymous speaker. `line` is the full body text;
 * `type_progress` is how many characters (float, truncated) of it are
 * visible, so callers drive the reveal speed.
 */
void hud_dialog_draw(const char* name, const char* line, float type_progress, int page);

/*
 * Begin a conversation. `lines` must point at storage that outlives the
 * conversation (static tables are the intended use). The E press on the frame
 * that opened the box is consumed, so it cannot instantly skip the first line.
 */
void dialog_start(const char* name, const char* const* lines, int line_count);

/* Non-zero while the box is on screen. */
int dialog_active(void);

/* 1 when the current line is fully revealed (waiting for the advancing E). */
int dialog_line_complete(void);

/* 0-based page of the wrapped line currently shown (island1-s81y). */
int dialog_page(void);

/* Pages the current wrapped line has at the active viewport size. */
int dialog_page_count(void);

/* Hide the box immediately (used by the advancing E and by reset paths). */
void dialog_close(void);

/*
 * Advance the typewriter by `dt` seconds and apply the E rule when
 * `advance_pressed` is non-zero (rising edge, ACT_INTERACT).
 */
void dialog_update(float dt, int advance_pressed);

/* Draw the active conversation (no-op when closed). Call last in the HUD pass. */
void dialog_draw(void);

#endif
