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
/* Glyph cell of the 8x8 font, and the row step at a given scale (+2px gap). */
#define DIALOG_GLYPH 8
/* Integer text scales only: fractional scales misalign the run-merged glyph
 * quads. Layout picks the first scale whose wrapped line fits the box. */
#define DIALOG_SCALE_2X 2.0f
#define DIALOG_SCALE_1X 1.0f
#define DIALOG_ROW_GAP 2.0f
/* Wrap output cap: all rows of one line, separate from DIALOG_MAX_LINES
 * (conversation line count). 16 rows x the worst small-screen row (19 chars
 * at 240px) covers any line far past DIALOG_MAX_CHARS. */
#define DIALOG_WRAP_ROWS 16

/* ---- Conversation state ------------------------------------------------- */

static const char* s_name = NULL;
/* Line pointers are COPIED on dialog_start(): callers routinely pass a
 * stack-local array of pointers, so storing the array address would dangle
 * into a dead frame the moment the caller returns. The strings themselves
 * must still outlive the conversation (static tables are the intended use). */
static const char* s_line_ptrs[DIALOG_MAX_LINES];
static const char* const* s_lines = NULL;
static int s_line_count = 0;
static int s_line = 0;
static float s_char_time = 0.0f;
static int s_consume_press = 0; /* swallow the E that opened the box */
static int s_page = 0;          /* visible page of the wrapped line */

/* ---- Shared layout (draw + E rule must agree on scale/page rows) -------- */

static int wrap_line(const char* line, int max_chars, int max_rows, int* row_starts, int* row_lens);

typedef struct {
  int bx, by, bw, bh;
  float scale;
  int cell;      /* 8 * scale */
  int row_step;  /* cell + gap */
  int max_chars; /* body characters per row */
  int fit_rows;  /* body rows visible in the box */
} DialogLayout;

static void dialog_layout(DialogLayout* L, int vw, int vh, const char* name, const char* worst);

/* Longest line of the active conversation (stable scale across lines), or
 * `line` when drawn standalone. */
static const char* dialog_worst_line(const char* line) {
  if (s_lines) {
    const char* worst = s_lines[0];
    for (int i = 1; i < s_line_count; i++)
      if (strlen(s_lines[i]) > strlen(worst))
        worst = s_lines[i];
    return worst;
  }
  return line;
}

/* Wrapped row count of `line` at `max_chars` (capped at DIALOG_WRAP_ROWS). */
static int dialog_row_count(const char* line, int max_chars) {
  int starts[DIALOG_WRAP_ROWS], lens[DIALOG_WRAP_ROWS];
  return wrap_line(line, max_chars, DIALOG_WRAP_ROWS, starts, lens);
}

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
  for (int i = 0; i < line_count; i++)
    s_line_ptrs[i] = lines[i];
  s_lines = s_line_ptrs;
  s_line_count = line_count;
  s_line = 0;
  s_page = 0;
  s_char_time = 0.0f;
  s_consume_press = 1;
}

int dialog_active(void) {
  return s_lines != NULL;
}

int dialog_line_complete(void) {
  if (!s_lines)
    return 0;
  return dialog_visible_chars() >= (int)strlen(s_lines[s_line]);
}

void dialog_close(void) {
  s_lines = NULL;
}

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

  /* E rule: finish the line first, then page down if more wrapped rows sit
   * below, then advance; the last E closes the box. Paging (island1-s81y)
   * guarantees every row is readable on small screens. */
  if (!dialog_line_complete()) {
    s_char_time = (float)strlen(s_lines[s_line]) * DIALOG_CHAR_PERIOD;
    return;
  }
  const RenderFrame* fr = r_frame();
  if (fr->width > 0 && fr->height > 0) {
    DialogLayout L;
    dialog_layout(&L, fr->width, fr->height, s_name, dialog_worst_line(s_lines[s_line]));
    int total = dialog_row_count(s_lines[s_line], L.max_chars);
    if (L.fit_rows >= 1 && s_page + L.fit_rows < total) {
      s_page += L.fit_rows;
      return;
    }
  }
  s_line++;
  s_page = 0;
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
static void rect_px(int vw, int vh, int x, int y, int w, int h, const float color[4]) {
  r_rect_2d((float)x / (float)vw, 1.0f - (float)(y + h) / (float)vh, (float)w / (float)vw, (float)h / (float)vh, color);
}

/*
 * Word-wrap `line` into rows that fit `max_chars`. Fills `row_starts` /
 * `row_lens` (at most DIALOG_MAX_LINES rows of the wrapped body that fit the
 * box height) and returns the row count. Spaces at wrap points are kept in the
 * row so character indices stay identical to the original string; the reveal
 * count indexes into `line` seamlessly across rows.
 */
static int wrap_line(const char* line, int max_chars, int max_rows, int* row_starts, int* row_lens) {
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

/*
 * Pick box geometry, text scale and body capacity for this viewport.
 * Tries integer scales largest-first and takes the first whose wrapped
 * `worst` line fits the box (speaker row included); falls back to scale 1
 * with whatever rows fit - paging covers the remainder, so nothing is ever
 * clipped in silence.
 */
static void dialog_layout(DialogLayout* L, int vw, int vh, const char* name, const char* worst) {
  dialog_box(vw, vh, &L->bx, &L->by, &L->bw, &L->bh);
  int inner_w = L->bw - 2 * DIALOG_PAD_PX;
  const float scales[2] = {DIALOG_SCALE_2X, DIALOG_SCALE_1X};

  for (int i = 0; i < 2; i++) {
    int cell = (int)(DIALOG_GLYPH * scales[i]);
    int row_step = (int)(DIALOG_GLYPH * scales[i] + DIALOG_ROW_GAP);
    int max_chars = inner_w / cell;
    if (max_chars <= 0)
      continue;
    int name_h = name ? row_step + DIALOG_PAD_PX / 2 : 0;
    int fit = (L->bh - 2 * DIALOG_PAD_PX - name_h) / row_step;
    if (fit < 1)
      continue;
    if (dialog_row_count(worst, max_chars) <= fit) {
      L->scale = scales[i];
      L->cell = cell;
      L->row_step = row_step;
      L->max_chars = max_chars;
      L->fit_rows = fit;
      return;
    }
  }

  /* No scale fits: smallest text, as many rows as genuinely fit. */
  L->scale = DIALOG_SCALE_1X;
  L->cell = DIALOG_GLYPH;
  L->row_step = (int)(DIALOG_GLYPH + DIALOG_ROW_GAP);
  L->max_chars = inner_w / L->cell;
  if (L->max_chars < 1)
    L->max_chars = 1;
  int name_h = name ? L->row_step + DIALOG_PAD_PX / 2 : 0;
  L->fit_rows = (L->bh - 2 * DIALOG_PAD_PX - name_h) / L->row_step;
  if (L->fit_rows < 1)
    L->fit_rows = 1;
}

/* Blinking down triangle (no font glyph): 4 stacked strips, widest on top. */
static void tri_down(int vw, int vh, int cx, int top, int w, int h, const float color[4]) {
  int row_h = h / 4;
  if (row_h < 1)
    row_h = 1;
  for (int i = 0; i < 4; i++) {
    int rw = w - (i * w) / 4;
    if (rw < 1)
      rw = 1;
    rect_px(vw, vh, cx - rw / 2, top + i * row_h, rw, row_h, color);
  }
}

void hud_dialog_draw(const char* name, const char* line, float type_progress, int page) {
  r_pass(R_PASS_HUD); /* the box is definitionally HUD; also survives callers
                         that forgot to set the pass */

  const RenderFrame* fr = r_frame();
  int vw = fr->width, vh = fr->height;
  if (vw <= 0 || vh <= 0 || !line)
    return;

  const float black[4] = {0.0f, 0.0f, 0.0f, 1.0f};
  const float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};

  DialogLayout L;
  dialog_layout(&L, vw, vh, name, dialog_worst_line(line));

  /* Fill, then a 2px white frame as four edge strips. */
  rect_px(vw, vh, L.bx, L.by, L.bw, L.bh, black);
  rect_px(vw, vh, L.bx, L.by, L.bw, DIALOG_BORDER_PX, white);
  rect_px(vw, vh, L.bx, L.by + L.bh - DIALOG_BORDER_PX, L.bw, DIALOG_BORDER_PX, white);
  rect_px(vw, vh, L.bx, L.by, DIALOG_BORDER_PX, L.bh, white);
  rect_px(vw, vh, L.bx + L.bw - DIALOG_BORDER_PX, L.by, DIALOG_BORDER_PX, L.bh, white);

  int text_x = L.bx + DIALOG_PAD_PX;
  int text_y = L.by + DIALOG_PAD_PX;

  if (name)
    text_y += L.row_step + DIALOG_PAD_PX / 2;

  int total = dialog_row_count(line, L.max_chars);
  int first = page;
  if (first < 0)
    first = 0;
  if (L.fit_rows >= 1) {
    int last = ((total - 1) / L.fit_rows) * L.fit_rows;
    if (last < 0)
      last = 0;
    if (first > last)
      first = last;
  }

  int vis = (int)type_progress;
  if (vis < 0)
    vis = 0;

  char buf[DIALOG_MAX_CHARS + 1];
  int starts[DIALOG_WRAP_ROWS], lens[DIALOG_WRAP_ROWS];
  wrap_line(line, L.max_chars, DIALOG_WRAP_ROWS, starts, lens);
  int row = 0;
  for (int r = first; r < total && row < L.fit_rows; r++, row++) {
    int shown = vis - starts[r];
    if (shown <= 0)
      break; /* nothing of this row revealed yet */
    if (shown > lens[r])
      shown = lens[r];
    if (shown > DIALOG_MAX_CHARS)
      shown = DIALOG_MAX_CHARS; /* row buffer bound, see the macro */
    memcpy(buf, line + starts[r], (size_t)shown);
    buf[shown] = '\0';
    if (shown > 0)
      r_text_2d(text_x, text_y + row * L.row_step, L.scale, buf, white);
  }

  /* Speaker name sits above the body rows. */
  if (name)
    r_text_2d(text_x, L.by + DIALOG_PAD_PX, L.scale, name, white);
}

void dialog_draw(void) {
  if (!s_lines)
    return;

  hud_dialog_draw(s_name, s_lines[s_line], (float)dialog_visible_chars(), s_page);

  const RenderFrame* fr = r_frame();
  if (fr->width <= 0 || fr->height <= 0)
    return;
  const float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
  DialogLayout L;
  dialog_layout(&L, fr->width, fr->height, s_name, dialog_worst_line(s_lines[s_line]));
  int total = dialog_row_count(s_lines[s_line], L.max_chars);
  int more = L.fit_rows >= 1 && s_page + L.fit_rows < total;
  int phase = (int)(s_char_time * 2.0f); /* 2 Hz blink */
  int blink = !(phase & 1);

  /* Down triangle left of the E hint while rows remain below this page. */
  if (more && blink)
    tri_down(fr->width, fr->height, L.bx + L.bw - DIALOG_PAD_PX - (int)(L.scale * 14.0f), L.by + L.bh - DIALOG_PAD_PX - (int)(L.scale * 8.0f), (int)(L.scale * 8.0f), (int)(L.scale * 6.0f), white);

  /* Blinking E hint at the bottom-right once the line is fully revealed. */
  if (!dialog_line_complete() || !blink)
    return;
  int e = (int)(DIALOG_GLYPH * L.scale);
  r_text_2d(L.bx + L.bw - DIALOG_PAD_PX - e, L.by + L.bh - DIALOG_PAD_PX - e, L.scale, "E", white);
}

int dialog_page(void) {
  if (!s_lines || s_page <= 0)
    return 0;
  const RenderFrame* fr = r_frame();
  if (fr->width <= 0 || fr->height <= 0)
    return 0;
  DialogLayout L;
  dialog_layout(&L, fr->width, fr->height, s_name, dialog_worst_line(s_lines[s_line]));
  if (L.fit_rows < 1)
    return 0;
  return s_page / L.fit_rows; /* s_page is a row offset; expose the index */
}

int dialog_page_count(void) {
  if (!s_lines || !dialog_line_complete())
    return 1; /* paging only exists once a line is fully revealed */
  const RenderFrame* fr = r_frame();
  if (fr->width <= 0 || fr->height <= 0)
    return 1;
  DialogLayout L;
  dialog_layout(&L, fr->width, fr->height, s_name, dialog_worst_line(s_lines[s_line]));
  int total = dialog_row_count(s_lines[s_line], L.max_chars);
  if (L.fit_rows < 1 || total <= L.fit_rows)
    return 1;
  return (total + L.fit_rows - 1) / L.fit_rows;
}
