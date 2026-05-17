#include "ghostty_bridge.h"

#ifdef TERM49_USE_GHOSTTY

#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <unicode/utf.h>

#include <ghostty/vt.h>

#include "io.h"

struct ghostty_bridge_state {
  GhosttyTerminal terminal;
  GhosttyRenderState render_state;
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
  if (!gb.initialized || data == NULL || len == 0) { return; }
  ghostty_terminal_vt_write(gb.terminal, data, len);
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

#else

int ghostty_bridge_link_probe(void) { return 0; }

#endif
