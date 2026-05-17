#ifndef GHOSTTY_BRIDGE_H_
#define GHOSTTY_BRIDGE_H_

#include <stddef.h>
#include <stdint.h>

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
} ghostty_bridge_frame_t;

typedef void (*ghostty_bridge_cell_visitor_t)(uint16_t x, uint16_t y,
                                             const ghostty_bridge_cell_t *cell,
                                             void *userdata);

int ghostty_bridge_init(uint16_t cols, uint16_t rows, size_t max_scrollback);
void ghostty_bridge_uninit(void);
void ghostty_bridge_write(const uint8_t *data, size_t len);
int ghostty_bridge_resize(uint16_t cols, uint16_t rows,
                          uint32_t cell_width_px, uint32_t cell_height_px);
int ghostty_bridge_update_render_state(void);
int ghostty_bridge_begin_frame(ghostty_bridge_frame_t *frame);
int ghostty_bridge_visit_cells(ghostty_bridge_cell_visitor_t visitor, void *userdata);
int ghostty_bridge_link_probe(void);

#endif /* GHOSTTY_BRIDGE_H_ */
