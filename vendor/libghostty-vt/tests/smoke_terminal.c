/* Standalone terminal API smoke test for BB10/QNX. Built with
 * qcc -V4.6.3,gcc_ntoarmv7le, linked against only libghostty-vt.a,
 * tests/shims.c, libm, and libgcc, then run on the Q10.
 *
 * Contract: feed "\x1b[31mhi"; PASS iff cells (0,0)/(1,0) contain
 * "h"/"i" and cell (0,0) has the named-red foreground color. */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <ghostty/vt.h>

/* Embedder allocator over QNX libc malloc/free. Term49 passes a real
 * allocator to libghostty-vt, so this standalone harness does too. */
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

static int read_cell(GhosttyTerminal terminal, uint16_t x, uint16_t y,
                     uint32_t *codepoint, GhosttyStyle *style) {
    GhosttyGridRef ref = GHOSTTY_INIT_SIZED(GhosttyGridRef);
    GhosttyPoint pt = {
        .tag = GHOSTTY_POINT_TAG_ACTIVE,
        .value = { .coordinate = { .x = x, .y = y } },
    };
    uint32_t graphemes[4] = {0};
    size_t graphemes_len = 0;

    if (ghostty_terminal_grid_ref(terminal, pt, &ref) != GHOSTTY_SUCCESS) return 0;
    if (ghostty_grid_ref_graphemes(&ref, graphemes,
                                   sizeof(graphemes) / sizeof(graphemes[0]),
                                   &graphemes_len) != GHOSTTY_SUCCESS) return 0;
    if (style != NULL && ghostty_grid_ref_style(&ref, style) != GHOSTTY_SUCCESS) return 0;

    if (codepoint != NULL) *codepoint = graphemes_len > 0 ? graphemes[0] : 0;
    return 1;
}

int main(void) {
    GhosttyTerminal terminal;
    GhosttyTerminalOptions opts = { .cols = 80, .rows = 24, .max_scrollback = 0 };
    if (ghostty_terminal_new(&GHA, &terminal, opts) != GHOSTTY_SUCCESS) {
        printf("SMOKE_FAIL: ghostty_terminal_new\n"); return 1;
    }

    const char *vt = "\x1b[31mhi";
    ghostty_terminal_vt_write(terminal, (const uint8_t *)vt, strlen(vt));

    uint32_t first = 0;
    uint32_t second = 0;
    GhosttyStyle style = GHOSTTY_INIT_SIZED(GhosttyStyle);
    int got_first = read_cell(terminal, 0, 0, &first, &style);
    int got_second = read_cell(terminal, 1, 0, &second, NULL);
    int text_ok = got_first && got_second && first == 'h' && second == 'i';
    int is_red = (style.fg_color.tag == GHOSTTY_STYLE_COLOR_PALETTE &&
                  style.fg_color.value.palette == GHOSTTY_COLOR_NAMED_RED);

    printf("cell0=U+%04x cell1=U+%04x fg_tag=%d fg_palette=%d text_ok=%d is_red=%d\n",
           (unsigned)first, (unsigned)second,
           (int)style.fg_color.tag, (int)style.fg_color.value.palette,
           text_ok, is_red);

    ghostty_terminal_free(terminal);

    if (text_ok && is_red) { printf("SMOKE_OK\n"); return 0; }
    printf("SMOKE_FAIL: text_ok=%d is_red=%d\n", text_ok, is_red);
    return 1;
}
