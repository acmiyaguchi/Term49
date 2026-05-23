#include "ghostty_bridge.h"

#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <unicode/utf.h>

#include <ghostty/vt.h>

#include "session.h"

struct ghostty_bridge {
  GhosttyTerminal terminal;
  GhosttyRenderState render_state;
  GhosttyRenderStateColors colors;
  int prev_write_was_cr;
};

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

/* Signature must match GhosttyTerminalWritePtyFn exactly: userdata is the
 * second arg. The session_t pointer flows through here from
 * ghostty_bridge_create so query responses (DA1, OSC color, etc.) reach the
 * correct pty even with multiple tabs (#4 step 3). */
static void gb_write_pty(GhosttyTerminal terminal, void *userdata,
                         const uint8_t *data, size_t len) {
  (void)terminal;
  if (data == NULL || len == 0) { return; }
  session_write_bytes((session_t *)userdata, (const char *)data, len);
}

int ghostty_bridge_create(ghostty_bridge_t **out, uint16_t cols, uint16_t rows,
                          size_t max_scrollback, void *userdata) {
  ghostty_bridge_t *b;

  if (out == NULL) { return -1; }

  b = calloc(1, sizeof(*b));
  if (b == NULL) { return -1; }

  GhosttyTerminalOptions opts = {
    .cols = cols,
    .rows = rows,
    .max_scrollback = max_scrollback,
  };

  if (ghostty_terminal_new(&GB_ALLOC, &b->terminal, opts) != GHOSTTY_SUCCESS) {
    free(b);
    return -1;
  }
  if (ghostty_render_state_new(&GB_ALLOC, &b->render_state) != GHOSTTY_SUCCESS) {
    ghostty_terminal_free(b->terminal);
    free(b);
    return -1;
  }
  ghostty_terminal_set(b->terminal, GHOSTTY_TERMINAL_OPT_USERDATA, userdata);
  ghostty_terminal_set(b->terminal, GHOSTTY_TERMINAL_OPT_WRITE_PTY, gb_write_pty);

  *out = b;
  return 0;
}

void ghostty_bridge_destroy(ghostty_bridge_t *b) {
  if (b == NULL) { return; }
  ghostty_render_state_free(b->render_state);
  ghostty_terminal_free(b->terminal);
  free(b);
}

void ghostty_bridge_write(ghostty_bridge_t *b, const uint8_t *data, size_t len) {
  size_t run_start = 0;
  size_t i;

  if (b == NULL || data == NULL || len == 0) { return; }

  /* BB10's pty output can contain bare LF. A terminal LF moves down without
   * carriage-returning, creating staircase newlines. Normalize bare LF to
   * CRLF before handing bytes to Ghostty, while tracking CR across chunks so
   * real CRLF streams are not doubled. */
  for (i = 0; i < len; ++i) {
    if (data[i] == '\n' && !b->prev_write_was_cr) {
      static const uint8_t crlf[2] = { '\r', '\n' };
      if (i > run_start) {
        ghostty_terminal_vt_write(b->terminal, data + run_start, i - run_start);
      }
      ghostty_terminal_vt_write(b->terminal, crlf, sizeof(crlf));
      run_start = i + 1;
    }
    b->prev_write_was_cr = data[i] == '\r';
  }

  if (run_start < len) {
    ghostty_terminal_vt_write(b->terminal, data + run_start, len - run_start);
  }
}

int ghostty_bridge_resize(ghostty_bridge_t *b, uint16_t cols, uint16_t rows,
                          uint32_t cell_width_px, uint32_t cell_height_px) {
  if (b == NULL) { return -1; }
  return ghostty_terminal_resize(b->terminal, cols, rows,
                                 cell_width_px, cell_height_px) == GHOSTTY_SUCCESS ? 0 : -1;
}

static int gb_update_render_state(ghostty_bridge_t *b) {
  return ghostty_render_state_update(b->render_state, b->terminal) == GHOSTTY_SUCCESS ? 0 : -1;
}

int ghostty_bridge_begin_frame(ghostty_bridge_t *b, ghostty_bridge_frame_t *frame) {
  bool cursor_has_value = false;

  if (b == NULL || frame == NULL) { return -1; }
  if (gb_update_render_state(b) != 0) { return -1; }

  b->colors = GHOSTTY_INIT_SIZED(GhosttyRenderStateColors);
  if (ghostty_render_state_colors_get(b->render_state, &b->colors) != GHOSTTY_SUCCESS) {
    return -1;
  }

  frame->cols = 0;
  frame->rows = 0;
  frame->default_fg = gb_rgb(b->colors.foreground);
  frame->default_bg = gb_rgb(b->colors.background);
  frame->cursor_visible = 0;
  frame->cursor_x = 0;
  frame->cursor_y = 0;
  frame->cursor_wide_tail = 0;
  frame->dirty = GHOSTTY_RENDER_STATE_DIRTY_FULL;

  ghostty_render_state_get(b->render_state, GHOSTTY_RENDER_STATE_DATA_DIRTY, &frame->dirty);
  ghostty_render_state_get(b->render_state, GHOSTTY_RENDER_STATE_DATA_COLS, &frame->cols);
  ghostty_render_state_get(b->render_state, GHOSTTY_RENDER_STATE_DATA_ROWS, &frame->rows);
  ghostty_render_state_get(b->render_state, GHOSTTY_RENDER_STATE_DATA_CURSOR_VIEWPORT_HAS_VALUE, &cursor_has_value);
  if (cursor_has_value) {
    bool cursor_visible = false;
    bool cursor_wide_tail = false;
    ghostty_render_state_get(b->render_state, GHOSTTY_RENDER_STATE_DATA_CURSOR_VISIBLE, &cursor_visible);
    ghostty_render_state_get(b->render_state, GHOSTTY_RENDER_STATE_DATA_CURSOR_VIEWPORT_X, &frame->cursor_x);
    ghostty_render_state_get(b->render_state, GHOSTTY_RENDER_STATE_DATA_CURSOR_VIEWPORT_Y, &frame->cursor_y);
    ghostty_render_state_get(b->render_state, GHOSTTY_RENDER_STATE_DATA_CURSOR_VIEWPORT_WIDE_TAIL, &cursor_wide_tail);
    frame->cursor_visible = cursor_visible ? 1 : 0;
    frame->cursor_wide_tail = cursor_wide_tail ? 1 : 0;
  }

  return 0;
}

static int gb_resolve_style_color(ghostty_bridge_t *b, GhosttyStyleColor color,
                                  ghostty_bridge_rgb_t *out) {
  if (out == NULL) { return 0; }
  switch (color.tag) {
  case GHOSTTY_STYLE_COLOR_PALETTE:
    *out = gb_rgb(b->colors.palette[color.value.palette]);
    return 1;
  case GHOSTTY_STYLE_COLOR_RGB:
    *out = gb_rgb(color.value.rgb);
    return 1;
  case GHOSTTY_STYLE_COLOR_NONE:
  default:
    return 0;
  }
}

static void gb_init_cell(ghostty_bridge_cell_t *cell) {
  cell->has_text = 0;
  cell->codepoint = 0;
  cell->has_fg = 0;
  cell->has_bg = 0;
  cell->fg.r = cell->fg.g = cell->fg.b = 0;
  cell->bg.r = cell->bg.g = cell->bg.b = 0;
  cell->bold = 0;
  cell->italic = 0;
  cell->underline = 0;
  cell->inverse = 0;
  cell->invisible = 0;
  cell->wide_tail = 0;
}

static void gb_read_render_cell(ghostty_bridge_t *b,
                                GhosttyRenderStateRowCells cells,
                                ghostty_bridge_cell_t *cell) {
  GhosttyStyle style = GHOSTTY_INIT_SIZED(GhosttyStyle);
  GhosttyCell raw = 0;
  GhosttyCellWide wide = GHOSTTY_CELL_WIDE_NARROW;
  GhosttyColorRgb color;
  uint32_t graphemes_len = 0;

  gb_init_cell(cell);

  if (ghostty_render_state_row_cells_get(cells,
        GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_GRAPHEMES_LEN,
        &graphemes_len) == GHOSTTY_SUCCESS && graphemes_len > 0) {
    uint32_t stack_buf[8];
    uint32_t *buf = stack_buf;

    if (graphemes_len > sizeof(stack_buf) / sizeof(stack_buf[0])) {
      buf = (uint32_t *)malloc((size_t)graphemes_len * sizeof(uint32_t));
    }

    if (buf != NULL &&
        ghostty_render_state_row_cells_get(cells,
          GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_GRAPHEMES_BUF,
          buf) == GHOSTTY_SUCCESS) {
      cell->has_text = 1;
      cell->codepoint = buf[0];
    }

    if (buf != stack_buf) { free(buf); }
  }

  if (ghostty_render_state_row_cells_get(cells,
        GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_RAW,
        &raw) == GHOSTTY_SUCCESS) {
    ghostty_cell_get(raw, GHOSTTY_CELL_DATA_WIDE, &wide);
    cell->wide_tail = (wide == GHOSTTY_CELL_WIDE_SPACER_TAIL ||
                       wide == GHOSTTY_CELL_WIDE_SPACER_HEAD) ? 1 : 0;
  }

  if (ghostty_render_state_row_cells_get(cells,
        GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_STYLE,
        &style) == GHOSTTY_SUCCESS) {
    cell->bold = style.bold ? 1 : 0;
    cell->italic = style.italic ? 1 : 0;
    cell->underline = style.underline != 0 ? 1 : 0;
    cell->inverse = style.inverse ? 1 : 0;
    cell->invisible = style.invisible ? 1 : 0;
  }

  if (ghostty_render_state_row_cells_get(cells,
        GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_FG_COLOR,
        &color) == GHOSTTY_SUCCESS) {
    cell->has_fg = 1;
    cell->fg = gb_rgb(color);
  } else if (gb_resolve_style_color(b, style.fg_color, &cell->fg)) {
    cell->has_fg = 1;
  }

  if (ghostty_render_state_row_cells_get(cells,
        GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_BG_COLOR,
        &color) == GHOSTTY_SUCCESS) {
    cell->has_bg = 1;
    cell->bg = gb_rgb(color);
  } else if (gb_resolve_style_color(b, style.bg_color, &cell->bg)) {
    cell->has_bg = 1;
  }
}

int ghostty_bridge_visit_cells(ghostty_bridge_t *b, int dirty_only,
                               ghostty_bridge_cell_visitor_t visitor, void *userdata) {
  GhosttyRenderStateRowIterator row_iter = NULL;
  GhosttyRenderStateRowCells cells = NULL;
  uint16_t y = 0;
  int rc = -1;

  if (b == NULL || visitor == NULL) { return -1; }
  if (ghostty_render_state_row_iterator_new(&GB_ALLOC, &row_iter) != GHOSTTY_SUCCESS) { return -1; }
  if (ghostty_render_state_row_cells_new(&GB_ALLOC, &cells) != GHOSTTY_SUCCESS) { goto done; }
  if (ghostty_render_state_get(b->render_state,
        GHOSTTY_RENDER_STATE_DATA_ROW_ITERATOR,
        &row_iter) != GHOSTTY_SUCCESS) { goto done; }

  while (ghostty_render_state_row_iterator_next(row_iter)) {
    bool row_dirty = true;
    uint16_t x = 0;
    bool clean = false;

    if (dirty_only) {
      if (ghostty_render_state_row_get(row_iter,
            GHOSTTY_RENDER_STATE_ROW_DATA_DIRTY,
            &row_dirty) != GHOSTTY_SUCCESS) { goto done; }
      if (!row_dirty) {
        ++y;
        continue;
      }
    }

    if (ghostty_render_state_row_get(row_iter,
          GHOSTTY_RENDER_STATE_ROW_DATA_CELLS,
          &cells) != GHOSTTY_SUCCESS) { goto done; }

    while (ghostty_render_state_row_cells_next(cells)) {
      ghostty_bridge_cell_t cell;
      gb_read_render_cell(b, cells, &cell);
      visitor(x, y, &cell, userdata);
      ++x;
    }

    ghostty_render_state_row_set(row_iter,
      GHOSTTY_RENDER_STATE_ROW_OPTION_DIRTY,
      &clean);
    ++y;
  }

  rc = 0;

done:
  ghostty_render_state_row_cells_free(cells);
  ghostty_render_state_row_iterator_free(row_iter);
  return rc;
}

int ghostty_bridge_visit_row(ghostty_bridge_t *b, uint16_t target_y,
                             ghostty_bridge_cell_visitor_t visitor, void *userdata) {
  GhosttyRenderStateRowIterator row_iter = NULL;
  GhosttyRenderStateRowCells cells = NULL;
  uint16_t y = 0;
  int rc = -1;

  if (b == NULL || visitor == NULL) { return -1; }
  if (ghostty_render_state_row_iterator_new(&GB_ALLOC, &row_iter) != GHOSTTY_SUCCESS) { return -1; }
  if (ghostty_render_state_row_cells_new(&GB_ALLOC, &cells) != GHOSTTY_SUCCESS) { goto done; }
  if (ghostty_render_state_get(b->render_state,
        GHOSTTY_RENDER_STATE_DATA_ROW_ITERATOR,
        &row_iter) != GHOSTTY_SUCCESS) { goto done; }

  while (ghostty_render_state_row_iterator_next(row_iter)) {
    if (y == target_y) {
      uint16_t x = 0;
      if (ghostty_render_state_row_get(row_iter,
            GHOSTTY_RENDER_STATE_ROW_DATA_CELLS,
            &cells) != GHOSTTY_SUCCESS) { goto done; }

      while (ghostty_render_state_row_cells_next(cells)) {
        ghostty_bridge_cell_t cell;
        gb_read_render_cell(b, cells, &cell);
        visitor(x, y, &cell, userdata);
        ++x;
      }

      rc = 0;
      goto done;
    }
    ++y;
  }

  rc = 0;

done:
  ghostty_render_state_row_cells_free(cells);
  ghostty_render_state_row_iterator_free(row_iter);
  return rc;
}

int ghostty_bridge_finish_frame(ghostty_bridge_t *b) {
  GhosttyRenderStateDirty clean = GHOSTTY_RENDER_STATE_DIRTY_FALSE;
  if (b == NULL) { return -1; }
  return ghostty_render_state_set(b->render_state,
    GHOSTTY_RENDER_STATE_OPTION_DIRTY,
    &clean) == GHOSTTY_SUCCESS ? 0 : -1;
}

static void gb_scroll_viewport(ghostty_bridge_t *b,
                               GhosttyTerminalScrollViewportTag tag,
                               int delta_rows) {
  if (b == NULL) { return; }
  if (tag == GHOSTTY_SCROLL_VIEWPORT_DELTA && delta_rows == 0) { return; }
  GhosttyTerminalScrollViewport behavior = {
    .tag = tag,
    .value = { .delta = (intptr_t)delta_rows },
  };
  ghostty_terminal_scroll_viewport(b->terminal, behavior);
}

int ghostty_bridge_scroll_view(ghostty_bridge_t *b, int delta_rows) {
  gb_scroll_viewport(b, GHOSTTY_SCROLL_VIEWPORT_DELTA, delta_rows);
  return 0;
}

int ghostty_bridge_scroll_to_bottom(ghostty_bridge_t *b) {
  gb_scroll_viewport(b, GHOSTTY_SCROLL_VIEWPORT_BOTTOM, 0);
  return 0;
}

int ghostty_bridge_is_alt_screen(ghostty_bridge_t *b) {
  GhosttyTerminalScreen screen = GHOSTTY_TERMINAL_SCREEN_PRIMARY;
  if (b == NULL) { return 0; }
  if (ghostty_terminal_get(b->terminal,
        GHOSTTY_TERMINAL_DATA_ACTIVE_SCREEN,
        &screen) != GHOSTTY_SUCCESS) {
    return 0;
  }
  return screen == GHOSTTY_TERMINAL_SCREEN_ALTERNATE ? 1 : 0;
}

static int gb_mode_on(ghostty_bridge_t *b, GhosttyMode mode) {
  bool v = false;
  if (ghostty_terminal_mode_get(b->terminal, mode, &v) != GHOSTTY_SUCCESS) {
    return 0;
  }
  return v ? 1 : 0;
}

int ghostty_bridge_mouse_wheel_ready(ghostty_bridge_t *b) {
  if (b == NULL) { return 0; }
  /* Mouse tracking is independent of the alternate screen: an app can grab
   * the mouse while drawing on the primary screen (e.g. fen). When it has,
   * the wheel belongs to the app on either screen, matching xterm. We do
   * not gate on alt-screen here — scrollback is only the terminal's job
   * when no app is tracking the mouse. SGR is still required because we
   * only ever emit mode-1006 encoding. */
  if (!gb_mode_on(b, GHOSTTY_MODE_SGR_MOUSE)) { return 0; }
  return gb_mode_on(b, GHOSTTY_MODE_NORMAL_MOUSE) ||
         gb_mode_on(b, GHOSTTY_MODE_BUTTON_MOUSE) ||
         gb_mode_on(b, GHOSTTY_MODE_ANY_MOUSE);
}
