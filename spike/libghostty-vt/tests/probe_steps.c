/* Diagnostic harness for the libghostty-vt BB10 integration sequence.
 * Unlike spike_main.c, this avoids the formatter path so we can tell whether
 * the remaining Q10 crash is formatter-specific or in terminal/render state. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <ghostty/vt.h>

static void *gha_alloc(void *ctx, size_t len, uint8_t alignment, uintptr_t ra) {
    (void)ctx; (void)ra;
    size_t a = alignment < 1 ? 1 : alignment;
    void *base = malloc(len + a + sizeof(void *));
    if (!base) return NULL;
    uintptr_t raw = (uintptr_t)base + sizeof(void *);
    uintptr_t aligned = (raw + (a - 1)) & ~((uintptr_t)a - 1);
    ((void **)aligned)[-1] = base;
    return (void *)aligned;
}
static bool gha_resize(void *c, void *m, size_t ml, uint8_t a, size_t nl, uintptr_t r) {
    (void)c; (void)m; (void)ml; (void)a; (void)nl; (void)r; return false;
}
static void *gha_remap(void *c, void *m, size_t ml, uint8_t a, size_t nl, uintptr_t r) {
    (void)c; (void)m; (void)ml; (void)a; (void)nl; (void)r; return NULL;
}
static void gha_free(void *c, void *m, size_t ml, uint8_t a, uintptr_t r) {
    (void)c; (void)ml; (void)a; (void)r;
    if (m) free(((void **)m)[-1]);
}
static const GhosttyAllocatorVtable GHA_VT = { gha_alloc, gha_resize, gha_remap, gha_free };
static int gha_ctx;
static GhosttyAllocator GHA = { .ctx = &gha_ctx, .vtable = &GHA_VT };

static void step(const char *s) { printf("STEP %s\n", s); fflush(stdout); }

int main(void) {
    GhosttyTerminal terminal;
    GhosttyTerminalOptions opts = { .cols = 80, .rows = 24, .max_scrollback = 1000 };

    step("terminal_new before");
    if (ghostty_terminal_new(&GHA, &terminal, opts) != GHOSTTY_SUCCESS) {
        printf("PROBE_FAIL terminal_new\n"); return 1;
    }
    step("terminal_new after");

    const char *vt = "\x1b[31mhi";
    step("vt_write before");
    ghostty_terminal_vt_write(terminal, (const uint8_t *)vt, strlen(vt));
    step("vt_write after");

    uint16_t cx = 999, cy = 999;
    ghostty_terminal_get(terminal, GHOSTTY_TERMINAL_DATA_CURSOR_X, &cx);
    ghostty_terminal_get(terminal, GHOSTTY_TERMINAL_DATA_CURSOR_Y, &cy);
    printf("cursor=%u,%u\n", (unsigned)cx, (unsigned)cy);

    GhosttyGridRef ref = GHOSTTY_INIT_SIZED(GhosttyGridRef);
    GhosttyPoint pt = {
        .tag = GHOSTTY_POINT_TAG_ACTIVE,
        .value = { .coordinate = { .x = 0, .y = 0 } },
    };
    GhosttyStyle style = GHOSTTY_INIT_SIZED(GhosttyStyle);
    step("grid_ref before");
    GhosttyResult gr = ghostty_terminal_grid_ref(terminal, pt, &ref);
    GhosttyResult sr = gr == GHOSTTY_SUCCESS ? ghostty_grid_ref_style(&ref, &style) : gr;
    printf("grid_ref=%d style=%d fg_tag=%d fg_palette=%d\n", (int)gr, (int)sr,
           (int)style.fg_color.tag, (int)style.fg_color.value.palette);
    step("grid_ref after");

    GhosttyRenderState state;
    step("render_state_new before");
    if (ghostty_render_state_new(&GHA, &state) != GHOSTTY_SUCCESS) {
        printf("PROBE_FAIL render_state_new\n"); return 1;
    }
    step("render_state_new after");

    step("render_state_update before");
    GhosttyResult ur = ghostty_render_state_update(state, terminal);
    printf("render_update=%d\n", (int)ur);
    step("render_state_update after");

    GhosttyRenderStateDirty dirty = GHOSTTY_RENDER_STATE_DIRTY_FALSE;
    uint16_t rows = 0, cols = 0;
    ghostty_render_state_get(state, GHOSTTY_RENDER_STATE_DATA_DIRTY, &dirty);
    ghostty_render_state_get(state, GHOSTTY_RENDER_STATE_DATA_ROWS, &rows);
    ghostty_render_state_get(state, GHOSTTY_RENDER_STATE_DATA_COLS, &cols);
    printf("render dirty=%d size=%ux%u\n", (int)dirty, (unsigned)cols, (unsigned)rows);

    GhosttyRenderStateRowIterator iter;
    GhosttyRenderStateRowCells cells;
    step("render iter before");
    if (ghostty_render_state_row_iterator_new(&GHA, &iter) != GHOSTTY_SUCCESS ||
        ghostty_render_state_row_cells_new(&GHA, &cells) != GHOSTTY_SUCCESS) {
        printf("PROBE_FAIL render iter alloc\n"); return 1;
    }
    ghostty_render_state_get(state, GHOSTTY_RENDER_STATE_DATA_ROW_ITERATOR, iter);
    if (ghostty_render_state_row_iterator_next(iter)) {
        ghostty_render_state_row_get(iter, GHOSTTY_RENDER_STATE_ROW_DATA_CELLS, cells);
        if (ghostty_render_state_row_cells_next(cells)) {
            uint32_t glen = 0;
            uint32_t gbuf[8] = {0};
            GhosttyColorRgb fg = {0};
            GhosttyResult lr = ghostty_render_state_row_cells_get(cells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_GRAPHEMES_LEN, &glen);
            GhosttyResult br = ghostty_render_state_row_cells_get(cells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_GRAPHEMES_BUF, gbuf);
            GhosttyResult fr = ghostty_render_state_row_cells_get(cells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_FG_COLOR, &fg);
            printf("cell0 len=%u cp0=U+%04x len_r=%d buf_r=%d fg_r=%d fg=%u,%u,%u\n",
                   (unsigned)glen, (unsigned)gbuf[0], (int)lr, (int)br, (int)fr,
                   (unsigned)fg.r, (unsigned)fg.g, (unsigned)fg.b);
        }
    }
    step("render iter after");

    step("skip frees for exit probe");
    printf("PROBE_OK\n");
    fflush(stdout);
    return 0;
}
