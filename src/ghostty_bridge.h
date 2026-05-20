#ifndef GHOSTTY_BRIDGE_H_
#define GHOSTTY_BRIDGE_H_

#include <stddef.h>
#include <stdint.h>

#define GHOSTTY_BRIDGE_DIRTY_FALSE 0
#define GHOSTTY_BRIDGE_DIRTY_PARTIAL 1
#define GHOSTTY_BRIDGE_DIRTY_FULL 2

typedef struct ghostty_bridge_rgb {
  uint8_t r;
  uint8_t g;
  uint8_t b;
} ghostty_bridge_rgb_t;

typedef struct ghostty_bridge_cell {
  int has_text;
  uint32_t codepoint;
  int has_fg;
  int has_bg;
  ghostty_bridge_rgb_t fg;
  ghostty_bridge_rgb_t bg;
  int bold;
  int italic;
  int underline;
  int inverse;
  int invisible;
  int wide_tail;
} ghostty_bridge_cell_t;

typedef struct ghostty_bridge_frame {
  uint16_t cols;
  uint16_t rows;
  ghostty_bridge_rgb_t default_fg;
  ghostty_bridge_rgb_t default_bg;
  int cursor_visible;
  uint16_t cursor_x;
  uint16_t cursor_y;
  int cursor_wide_tail;
  int dirty;
} ghostty_bridge_frame_t;

typedef void (*ghostty_bridge_cell_visitor_t)(uint16_t x, uint16_t y,
                                             const ghostty_bridge_cell_t *cell,
                                             void *userdata);

int ghostty_bridge_init(uint16_t cols, uint16_t rows, size_t max_scrollback);
void ghostty_bridge_uninit(void);
void ghostty_bridge_write(const uint8_t *data, size_t len);
int ghostty_bridge_resize(uint16_t cols, uint16_t rows,
                          uint32_t cell_width_px, uint32_t cell_height_px);
int ghostty_bridge_begin_frame(ghostty_bridge_frame_t *frame);
int ghostty_bridge_visit_cells(int dirty_only, ghostty_bridge_cell_visitor_t visitor, void *userdata);
int ghostty_bridge_visit_row(uint16_t target_y, ghostty_bridge_cell_visitor_t visitor, void *userdata);
int ghostty_bridge_finish_frame(void);

/* Scrollback viewport control. delta_rows follows libghostty's convention:
 * negative scrolls up (back into history), positive scrolls down (toward live).
 * libghostty clamps internally, so callers do not need to bound the value. */
int ghostty_bridge_scroll_view(int delta_rows);
int ghostty_bridge_scroll_to_bottom(void);

/* 1 = alternate screen active (full-screen apps like vim/less own scrolling),
 * 0 = primary screen. Returns 0 if the bridge is not initialized. */
int ghostty_bridge_is_alt_screen(void);

/* 1 iff alt-screen is active AND any of ?1000/?1002/?1003 (mouse tracking)
 * is on AND ?1006 (SGR encoding) is on. Gates whether touch drag should
 * be translated into xterm wheel events for the running TUI. We never
 * emit legacy non-SGR mouse encoding. Returns 0 if the bridge is not
 * initialized. */
int ghostty_bridge_mouse_wheel_ready(void);

#endif /* GHOSTTY_BRIDGE_H_ */
