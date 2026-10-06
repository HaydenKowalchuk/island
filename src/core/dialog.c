#include "dialog.h"

#include <string.h>

#include "render.h"

/*
 * Typewriter reveal speed: one glyph every 30 ms (spec, island1-a2xf). Stored
 * as a period so the char count is just time / period.
 */
#define DIALOG_CHAR_PERIOD (1.0f / 30.0f)

/* Box layout: bottom third of the screen, ~70% width, centered (spec). */
#define DIALOG_BOX_W_FRAC 0.70f
#define DIALOG_BOX_H_FRAC (1.0f / 3.0f)
#define DIALOG_BORDER_PX 2
#define DIALOG_PAD_PX 8
/* Glyph cell: the 8x8 font at scale 2, with 2px breathing room between rows. */
#define DIALOG_GLYPH 8
#define DIALOG_TEXT_SCALE 2.0f
#define DIALOG_ROW_STEP (DIALOG_GLYPH * DIALOG_TEXT_SCALE + 2.0f)

/* ---- Conversation state ------------------------------------------------- */

static const char* s_name = NULL;
static const char* const* s_lines = NULL;
static int s_line_count = 0;
static int s_line = 0;
static float s_char_time = 0.0f;
static int s_consume_press = 0; /* swallow the E that opened the box */

static int dialog_visible_chars(void) {
  if (!s_lines)
    return 0;
  int len = (int)strlen(s_lines[s_line]);
  int vis = (int)(s_char_time / DIALOG_CHAR_PERIOD);
  return (vis < len) ? vis : len;
}

void dialog_start(const char* name, const char* const* lines, int line_count) {
  if (!lines || line_count <= 0)
    return;
  if (line_count > DIALOG_MAX_LINES)
    line_count = DIALOG_MAX_LINES;
  s_name = name;
  s_lines = lines;
  s_line_count = line_count;
  s_line = 0;
  s_char_time = 0.0f;
  s_consume_press = 1;
}

int dialog_active(void) { return s_lines != NULL; }

int dialog_line_complete(void) {
  if (!s_lines)
    return 0;
  return dialog_visible_chars() >= (int)strlen(s_lines[s_line]);
}

void dialog_close(void) { s_lines = NULL; }

void dialog_update(float dt, int advance_pressed) {
  if (!s_lines)
    return;

  if (s_consume_press) {
    s_consume_press = 0;
    advance_pressed = 0;
  }

  s_char_time += dt;
  if (!advance_pressed)
    return;

  /* E rule: finish the line first, then advance; the last E closes the box. */
  if (!dialog_line_complete()) {
    s_char_time = (float)strlen(s_lines[s_line]) * DIALOG_CHAR_PERIOD;
    return;
  }
  s_line++;
  s_char_time = 0.0f;
  if (s_line >= s_line_count)
    dialog_close();
}

/* ---- Textbox geometry ---------------------------------------------------- */

static void dialog_box(int vw, int vh, int* x, int* y, int* w, int* h) {
  *w = (int)((float)vw * DIALOG_BOX_W_FRAC);
  *h = (int)((float)vh * DIALOG_BOX_H_FRAC);
  *x = (vw - *w) / 2;
  *y = vh - *h;
}

/* Normalised [0,1] quad (origin bottom-left) from top-left pixel coords. */
static void rect_px(int vw, int vh, int x, int y, int w, int h,
                    const float color[4]) {
  r_rect_2d((float)x / (float)vw, 1.0f - (float)(y + h) / (float)vh,
            (float)w / (float)vw, (float)h / (float)vh, color);
}

/*
 * Word-wrap `line` into rows that fit `max_chars`. Fills `row_starts` /
 * `row_lens` (at most DIALOG_MAX_LINES rows of the wrapped body that fit the
 * box height) and returns the row count. Spaces at wrap points are kept in the
 * row so character indices stay identical to the original string; the reveal
 * count indexes into `line` seamlessly across rows.
 */
static int wrap_line(const char* line, int max_chars, int max_rows,
                     int* row_starts, int* row_lens) {
  int len = (int)strlen(line);
  int rows = 0, i = 0;

  while (i < len && rows < max_rows) {
    int start = i;
    int end = i + max_chars;
    if (end >= len) {
      end = len; /* final row, it fits */
    } else {
      /* Greedy: break at the last space in the row when there is one. */
      int brk = -1;
      for (int j = start; j < end; j++)
        if (line[j] == ' ')
          brk = j;
      if (brk > start)
        end = brk;
      /* No space in range: hard-break an over-long word at max_chars. */
    }
    row_starts[rows] = start;
    row_lens[rows] = end - start;
    rows++;
    i = end;
    while (i < len && line[i] == ' ')
      i++; /* spaces at the wrap point belong to neither row */
  }
  return rows;
}

void hud_dialog_draw(const char* name, const char* line, float type_progress) {
  r_pass(R_PASS_HUD); /* the box is definitionally HUD; also survives callers
                         that forgot to set the pass */

  const RenderFrame* fr = r_frame();
  int vw = fr->width, vh = fr->height;
  if (vw <= 0 || vh <= 0 || !line)
    return;

  const float black[4] = {0.0f, 0.0f, 0.0f, 1.0f};
  const float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};

  int bx, by, bw, bh;
  dialog_box(vw, vh, &bx, &by, &bw, &bh);

  /* Fill, then a 2px white frame as four edge strips. */
  rect_px(vw, vh, bx, by, bw, bh, black);
  rect_px(vw, vh, bx, by, bw, DIALOG_BORDER_PX, white);
  rect_px(vw, vh, bx, by + bh - DIALOG_BORDER_PX, bw, DIALOG_BORDER_PX, white);
  rect_px(vw, vh, bx, by, DIALOG_BORDER_PX, bh, white);
  rect_px(vw, vh, bx + bw - DIALOG_BORDER_PX, by, DIALOG_BORDER_PX, bh, white);

  int text_x = bx + DIALOG_PAD_PX;
  int text_y = by + DIALOG_PAD_PX;

  if (name) {
    r_text_2d(text_x, text_y, DIALOG_TEXT_SCALE, name, white);
    text_y += (int)DIALOG_ROW_STEP + DIALOG_PAD_PX / 2;
  }

  /* Body: word-wrapped rows, clipped to the box (the scissor stand-in). */
  int inner_w = bw - 2 * DIALOG_PAD_PX;
  int inner_h = by + bh - DIALOG_PAD_PX - text_y;
  int max_chars = inner_w / (int)(DIALOG_GLYPH * DIALOG_TEXT_SCALE);
  int max_rows = inner_h / (int)DIALOG_ROW_STEP;
  if (max_chars <= 0 || max_rows <= 0)
    return;

  int row_starts[DIALOG_MAX_LINES], row_lens[DIALOG_MAX_LINES];
  int rows = wrap_line(line, max_chars,
                       (max_rows < DIALOG_MAX_LINES) ? max_rows
                                                     : DIALOG_MAX_LINES,
                       row_starts, row_lens);

  int vis = (int)type_progress;
  if (vis < 0)
    vis = 0;

  char buf[DIALOG_MAX_CHARS + 1];
  for (int r = 0; r < rows; r++) {
    int shown = vis - row_starts[r];
    if (shown <= 0)
      break; /* nothing of this row revealed yet */
    if (shown > row_lens[r])
      shown = row_lens[r];
    if (shown > DIALOG_MAX_CHARS)
      shown = DIALOG_MAX_CHARS; /* row buffer bound, see comment on the macro */
    memcpy(buf, line + row_starts[r], (size_t)shown);
    buf[shown] = '\0';
    r_text_2d(text_x, text_y + r * (int)DIALOG_ROW_STEP, DIALOG_TEXT_SCALE,
              buf, white);
  }
}

void dialog_draw(void) {
  if (!s_lines)
    return;

  hud_dialog_draw(s_name, s_lines[s_line], (float)dialog_visible_chars());

  /* Blinking E hint at the bottom-right once the line is fully revealed. */
  if (!dialog_line_complete())
    return;
  int phase = (int)(s_char_time * 2.0f); /* 2 Hz blink */
  if (phase & 1)
    return;

  const RenderFrame* fr = r_frame();
  const float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
  int bx, by, bw, bh;
  dialog_box(fr->width, fr->height, &bx, &by, &bw, &bh);
  r_text_2d(bx + bw - DIALOG_PAD_PX - (int)(8.0f * DIALOG_TEXT_SCALE),
            by + bh - DIALOG_PAD_PX - (int)(8.0f * DIALOG_TEXT_SCALE),
            DIALOG_TEXT_SCALE, "E", white);
}
