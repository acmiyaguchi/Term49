#include "ghostty_bridge.h"

#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <unicode/utf.h>

#include <ghostty/vt.h>

#include "io.h"

struct ghostty_bridge_state {
  GhosttyTerminal terminal;
  GhosttyRenderState render_state;
  GhosttyRenderStateColors colors;
  int prev_write_was_cr;
  int initialized;
};

static struct ghostty_bridge_state gb;

static void *gb_alloc(void *ctx, size_t len, uint8_t alignment, uintptr_t ra) {
  (void)ctx;
  (void)ra;
  size_t a = alignment < 1 ? 1 : alignment;
  void *base = malloc(len + a + sizeof(void *));
  if (base == NULL) { return NULL; }

  uintptr_t raw = (uintptr_t)base + sizeof(void *);
  uintptr_t aligned = (raw + (a - 1)) & ~((uintptr_t)a - 1);
  ((void **)aligned)[-1] = base;
  return (void *)aligned;
}

static bool gb_resize(void *ctx, void *mem, size_t old_len,
                      uint8_t alignment, size_t new_len, uintptr_t ra) {
  (void)ctx;
  (void)mem;
  (void)old_len;
  (void)alignment;
  (void)new_len;
  (void)ra;
  return false;
}

static void *gb_remap(void *ctx, void *mem, size_t old_len,
                      uint8_t alignment, size_t new_len, uintptr_t ra) {
  (void)ctx;
  (void)mem;
  (void)old_len;
  (void)alignment;
  (void)new_len;
  (void)ra;
  return NULL;
}

static void gb_free(void *ctx, void *mem, size_t len, uint8_t alignment,
                    uintptr_t ra) {
  (void)ctx;
  (void)len;
  (void)alignment;
  (void)ra;
  if (mem != NULL) { free(((void **)mem)[-1]); }
}

static const GhosttyAllocatorVtable GB_ALLOC_VT = {
  gb_alloc,
  gb_resize,
  gb_remap,
  gb_free,
};
static int gb_alloc_ctx;
static GhosttyAllocator GB_ALLOC = { &gb_alloc_ctx, &GB_ALLOC_VT };

static ghostty_bridge_rgb_t gb_rgb(GhosttyColorRgb color) {
  ghostty_bridge_rgb_t rgb = { color.r, color.g, color.b };
  return rgb;
}

static void gb_write_pty(GhosttyTerminal terminal, const uint8_t *data,
                         size_t len, void *userdata) {
  (void)terminal;
  (void)userdata;
  if (data != NULL && len > 0) {
    io_write_master_char((const char *)data, len);
  }
}

int ghostty_bridge_link_probe(void) {
  bool simd = false;
  return ghostty_build_info(GHOSTTY_BUILD_INFO_SIMD, &simd) == GHOSTTY_SUCCESS && simd;
}

int ghostty_bridge_init(uint16_t cols, uint16_t rows, size_t max_scrollback) {
  if (gb.initialized) { return 0; }

  GhosttyTerminalOptions opts = {
    .cols = cols,
    .rows = rows,
    .max_scrollback = max_scrollback,
  };

  if (ghostty_terminal_new(&GB_ALLOC, &gb.terminal, opts) != GHOSTTY_SUCCESS) {
    return -1;
  }
  if (ghostty_render_state_new(&GB_ALLOC, &gb.render_state) != GHOSTTY_SUCCESS) {
    ghostty_terminal_free(gb.terminal);
    gb.terminal = NULL;
    return -1;
  }
  ghostty_terminal_set(gb.terminal, GHOSTTY_TERMINAL_OPT_WRITE_PTY, gb_write_pty);
  gb.initialized = 1;
  return 0;
}

void ghostty_bridge_uninit(void) {
  if (!gb.initialized) { return; }
  ghostty_render_state_free(gb.render_state);
  ghostty_terminal_free(gb.terminal);
  gb.render_state = NULL;
  gb.terminal = NULL;
  gb.initialized = 0;
}

void ghostty_bridge_write(const uint8_t *data, size_t len) {
  size_t run_start = 0;
  size_t i;

  if (!gb.initialized || data == NULL || len == 0) { return; }

  /* BB10's pty output reaches Term49 with bare LF in common shell paths.
   * A terminal LF moves down without carriage-returning, which creates the
   * classic staircase effect: every prompt/newline starts at the previous
   * column. Term49 historically treated cooked pty LF as CR+LF; preserve
   * that behavior before handing bytes to Ghostty. Keep a
   * cross-chunk CR flag so real CRLF streams are not doubled. */
  for (i = 0; i < len; ++i) {
    if (data[i] == '\n' && !gb.prev_write_was_cr) {
      static const uint8_t crlf[2] = { '\r', '\n' };
      if (i > run_start) {
        ghostty_terminal_vt_write(gb.terminal, data + run_start, i - run_start);
      }
      ghostty_terminal_vt_write(gb.terminal, crlf, sizeof(crlf));
      run_start = i + 1;
    }
    gb.prev_write_was_cr = data[i] == '\r';
  }

  if (run_start < len) {
    ghostty_terminal_vt_write(gb.terminal, data + run_start, len - run_start);
  }
}

int ghostty_bridge_resize(uint16_t cols, uint16_t rows,
                          uint32_t cell_width_px, uint32_t cell_height_px) {
  if (!gb.initialized) { return -1; }
  return ghostty_terminal_resize(gb.terminal, cols, rows,
                                 cell_width_px, cell_height_px) == GHOSTTY_SUCCESS ? 0 : -1;
}

int ghostty_bridge_update_render_state(void) {
  if (!gb.initialized) { return -1; }
  return ghostty_render_state_update(gb.render_state, gb.terminal) == GHOSTTY_SUCCESS ? 0 : -1;
}

int ghostty_bridge_begin_frame(ghostty_bridge_frame_t *frame) {
  bool cursor_has_value = false;

  if (!gb.initialized || frame == NULL) { return -1; }
  if (ghostty_bridge_update_render_state() != 0) { return -1; }

  gb.colors = GHOSTTY_INIT_SIZED(GhosttyRenderStateColors);
  if (ghostty_render_state_colors_get(gb.render_state, &gb.colors) != GHOSTTY_SUCCESS) {
    return -1;
  }

  frame->cols = 0;
  frame->rows = 0;
  frame->default_fg = gb_rgb(gb.colors.foreground);
  frame->default_bg = gb_rgb(gb.colors.background);
  frame->cursor_visible = 0;
  frame->cursor_x = 0;
  frame->cursor_y = 0;
  frame->cursor_wide_tail = 0;

  ghostty_render_state_get(gb.render_state, GHOSTTY_RENDER_STATE_DATA_COLS, &frame->cols);
  ghostty_render_state_get(gb.render_state, GHOSTTY_RENDER_STATE_DATA_ROWS, &frame->rows);
  ghostty_render_state_get(gb.render_state, GHOSTTY_RENDER_STATE_DATA_CURSOR_VIEWPORT_HAS_VALUE, &cursor_has_value);
  if (cursor_has_value) {
    bool cursor_visible = false;
    bool cursor_wide_tail = false;
    ghostty_render_state_get(gb.render_state, GHOSTTY_RENDER_STATE_DATA_CURSOR_VISIBLE, &cursor_visible);
    ghostty_render_state_get(gb.render_state, GHOSTTY_RENDER_STATE_DATA_CURSOR_VIEWPORT_X, &frame->cursor_x);
    ghostty_render_state_get(gb.render_state, GHOSTTY_RENDER_STATE_DATA_CURSOR_VIEWPORT_Y, &frame->cursor_y);
    ghostty_render_state_get(gb.render_state, GHOSTTY_RENDER_STATE_DATA_CURSOR_VIEWPORT_WIDE_TAIL, &cursor_wide_tail);
    frame->cursor_visible = cursor_visible ? 1 : 0;
    frame->cursor_wide_tail = cursor_wide_tail ? 1 : 0;
  }

  return 0;
}

static int gb_resolve_style_color(GhosttyStyleColor color, ghostty_bridge_rgb_t *out) {
  if (out == NULL) { return 0; }
  switch (color.tag) {
  case GHOSTTY_STYLE_COLOR_PALETTE:
    *out = gb_rgb(gb.colors.palette[color.value.palette]);
    return 1;
  case GHOSTTY_STYLE_COLOR_RGB:
    *out = gb_rgb(color.value.rgb);
    return 1;
  case GHOSTTY_STYLE_COLOR_NONE:
  default:
    return 0;
  }
}

int ghostty_bridge_visit_cells(ghostty_bridge_cell_visitor_t visitor, void *userdata) {
  uint16_t rows = 0;
  uint16_t cols = 0;
  uint16_t y;

  if (!gb.initialized || visitor == NULL) { return -1; }
  ghostty_render_state_get(gb.render_state, GHOSTTY_RENDER_STATE_DATA_ROWS, &rows);
  ghostty_render_state_get(gb.render_state, GHOSTTY_RENDER_STATE_DATA_COLS, &cols);

  for (y = 0; y < rows; ++y) {
    uint16_t x;
    for (x = 0; x < cols; ++x) {
      GhosttyGridRef ref = GHOSTTY_INIT_SIZED(GhosttyGridRef);
      GhosttyPoint pt = {
        .tag = GHOSTTY_POINT_TAG_VIEWPORT,
        .value = { .coordinate = { .x = x, .y = y } },
      };
      GhosttyStyle style = GHOSTTY_INIT_SIZED(GhosttyStyle);
      GhosttyCell raw = 0;
      GhosttyCellWide wide = GHOSTTY_CELL_WIDE_NARROW;
      uint32_t stack_buf[8];
      size_t graphemes_len = 0;
      ghostty_bridge_cell_t cell;

      cell.has_text = 0;
      cell.codepoint = 0;
      cell.has_fg = 0;
      cell.has_bg = 0;
      cell.fg.r = cell.fg.g = cell.fg.b = 0;
      cell.bg.r = cell.bg.g = cell.bg.b = 0;
      cell.bold = 0;
      cell.italic = 0;
      cell.underline = 0;
      cell.inverse = 0;
      cell.invisible = 0;
      cell.wide_tail = 0;

      if (ghostty_terminal_grid_ref(gb.terminal, pt, &ref) != GHOSTTY_SUCCESS) {
        visitor(x, y, &cell, userdata);
        continue;
      }

      if (ghostty_grid_ref_graphemes(&ref, stack_buf,
                                     sizeof(stack_buf) / sizeof(stack_buf[0]),
                                     &graphemes_len) == GHOSTTY_SUCCESS &&
          graphemes_len > 0) {
        cell.has_text = 1;
        cell.codepoint = stack_buf[0];
      } else if (graphemes_len > sizeof(stack_buf) / sizeof(stack_buf[0])) {
        uint32_t *buf = (uint32_t *)malloc(graphemes_len * sizeof(uint32_t));
        if (buf != NULL &&
            ghostty_grid_ref_graphemes(&ref, buf, graphemes_len, &graphemes_len) == GHOSTTY_SUCCESS &&
            graphemes_len > 0) {
          cell.has_text = 1;
          cell.codepoint = buf[0];
        }
        free(buf);
      }

      if (ghostty_grid_ref_cell(&ref, &raw) == GHOSTTY_SUCCESS) {
        ghostty_cell_get(raw, GHOSTTY_CELL_DATA_WIDE, &wide);
        cell.wide_tail = (wide == GHOSTTY_CELL_WIDE_SPACER_TAIL ||
                          wide == GHOSTTY_CELL_WIDE_SPACER_HEAD) ? 1 : 0;
      }

      if (ghostty_grid_ref_style(&ref, &style) == GHOSTTY_SUCCESS) {
        cell.bold = style.bold ? 1 : 0;
        cell.italic = style.italic ? 1 : 0;
        cell.underline = style.underline != 0 ? 1 : 0;
        cell.inverse = style.inverse ? 1 : 0;
        cell.invisible = style.invisible ? 1 : 0;
        cell.has_fg = gb_resolve_style_color(style.fg_color, &cell.fg);
        cell.has_bg = gb_resolve_style_color(style.bg_color, &cell.bg);
      }

      visitor(x, y, &cell, userdata);
    }
  }

  return 0;
}

