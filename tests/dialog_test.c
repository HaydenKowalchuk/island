/*
 * GL-free behaviour test for the conversation textbox (island1-a2xf).
 *
 * Checks the acceptance criteria without a window:
 *   - hud_dialog_draw records screen-space OVERLAY draws (box + border + text)
 *   - typewriter reveals at 1 char / 30 ms
 *   - the first E completes a partial line instantly, the next E advances,
 *     and the E after the last line closes the box
 *   - the E press that opened the conversation is consumed (no instant skip)
 */
#include "dialog.h"

#include <stdio.h>
#include <string.h>

#include "render.h"

static int failures = 0;

#define CHECK(cond, msg)         \
  do {                           \
    if (!(cond)) {               \
      printf("FAIL: %s\n", msg); \
      failures++;                \
    }                            \
  } while (0)

int main(void) {
  /* 1. The draw primitive records into the HUD/overlay pipeline. */
  r_frame_begin();
  r_viewport(320, 180);
  hud_dialog_draw("MAYOR", "WELCOME TO THE ISLAND.", 3.0f, 0);
  hud_dialog_draw(NULL, "WELCOME TO THE ISLAND.", 8.0f, 0); /* anonymous */
  const RenderFrame* fr = r_frame();
  CHECK(fr->draw_count > 0, "dialog records draws");
  CHECK(fr->pass_draws[R_PASS_HUD] == fr->draw_count, "dialog draws land in the HUD pass");
  CHECK(fr->stat_verts > 0, "dialog records vertices");

  /* 2. Typewriter timing: 30 ms per character. */
  static const char* const lines[] = {"HEY", "BYE"};
  r_frame_begin(); /* reset the recorder between sections */
  dialog_start("TESTER", lines, 2);
  CHECK(dialog_active(), "conversation opens");
  CHECK(!dialog_line_complete(), "fresh line is not complete");

  dialog_update(0.031f, 0); /* ~one char period */
  CHECK(!dialog_line_complete(), "3-char line incomplete after one period");

  dialog_update(0.100f, 0); /* past the end of a 3-char line */
  CHECK(dialog_line_complete(), "line completes after full reveal time");

  /* 3. E rule: first E completes, second E advances, last E closes. */
  r_frame_begin();
  dialog_start("TESTER", lines, 2);
  dialog_update(0.031f, 0); /* consume the opening frame, reveal one char */
  dialog_update(0.0f, 1);   /* partial line + E -> complete, stay on line 0 */
  CHECK(dialog_active() && dialog_line_complete(),
        "E completes a partial line without advancing");

  dialog_update(0.0f, 1); /* complete line + E -> advance to line 1 */
  CHECK(dialog_active() && !dialog_line_complete(),
        "E advances to the next (fresh) line");

  dialog_update(0.0f, 1); /* partial line + E -> complete again */
  CHECK(dialog_line_complete(), "second line completes on E too");

  dialog_update(0.0f, 1); /* complete last line + E -> close */
  CHECK(!dialog_active(), "E on the last line closes the box");

  /* 4. The opening press is consumed. */
  r_frame_begin();
  dialog_start("TESTER", lines, 2);
  dialog_update(0.0f, 1); /* same-frame E must not skip or advance */
  CHECK(dialog_active() && !dialog_line_complete(),
        "opening E press is consumed");

  /* 5. Draw path with an active conversation records text. */
  dialog_update(1.0f, 0);
  dialog_draw();
  CHECK(r_frame()->stat_verts > 0, "dialog_draw records the box");

  /* 6. Stack-local line arrays are safe: dialog_start() must copy the pointers,
   *    not keep the caller's array address (a dangling s_lines read stale
   *    stack and crashed strlen in the game). */
  dialog_close();
  r_frame_begin();
  {
    /* Open from a nested frame that dies before the box is touched. */
    const char* stack_lines[2] = {"STACK LINE A", "STACK LINE B"};
    dialog_start("STACKER", stack_lines, 2);
  }
  {
    /* Scrub the stack region the dead frame occupied. */
    volatile char scrub[512];
    memset((void*)scrub, 0x5A, sizeof(scrub));
    (void)scrub;
  }
  dialog_update(1.0f, 0);
  CHECK(dialog_active() && dialog_line_complete(),
        "line 0 survives the caller's frame dying");
  dialog_update(0.0f, 1); /* advance to line 1 */
  dialog_update(1.0f, 0);
  CHECK(dialog_active() && dialog_line_complete(),
        "line 1 survives the caller's frame dying");
  dialog_update(0.0f, 1); /* close */
  CHECK(!dialog_active(), "stack-started conversation closes normally");

  /* 7. Small screens (island1-s81y): at PSP 480x272 the layout drops to 1x
   *    text so a long residents-table line wraps entirely into the box -
   *    one page, nothing clipped. */
  {
    static const char* const long_lines[] = {
        "IF YOUR CAR WHINES IN THIRD IT'S THE BEARING. NOT THE SPIRITS, "
        "LIKE MARLOWE SAYS."};
    r_viewport(480, 272);
    dialog_start("TESTER", long_lines, 1);
    CHECK(strlen(long_lines[0]) >= 80, "line is representative of table copy");
    dialog_update(10.0f, 0);
    CHECK(dialog_line_complete(), "PSP-sized line fully reveals");
    CHECK(dialog_page_count() == 1,
          "PSP 480x272 fits a long line in one page");
    dialog_update(0.0f, 1); /* single page: E advances and closes */
    CHECK(!dialog_active(), "single-page long line closes on E");
  }

  /* 8. Below every fitting scale the line pages with E instead of clipping:
   *    exactly page_count() presses - one per page, the last closes. */
  {
    static const char* const paged_lines[] = {
        "THE LIGHTHOUSE KEEPER COUNTS THE SHIPS THAT NEVER ARRIVE AND "
        "WISHES ON EVERY SINGLE ONE FOR SOMETHING TO CHANGE."};
    r_viewport(240, 160);
    dialog_start("TESTER", paged_lines, 1);
    dialog_update(10.0f, 0);
    int pages = dialog_page_count();
    CHECK(pages > 1, "tiny viewport pages a long line");
    int clicks = 0;
    while (dialog_active() && clicks < 16) {
      CHECK(dialog_page() >= 0 && dialog_page() < pages,
            "page index stays in range");
      dialog_update(0.0f, 1);
      clicks++;
    }
    CHECK(!dialog_active(), "paging conversation closes after the last page");
    CHECK(clicks == pages, "one E per page, then close");
  }

  if (failures) {
    printf("%d dialog test failure(s)\n", failures);
    return 1;
  }
  printf("dialog: all checks passed\n");
  return 0;
}
