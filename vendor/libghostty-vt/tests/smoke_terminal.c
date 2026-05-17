/* Gate G — the standalone terminal API smoke test. qcc -V4.6.3,gcc_ntoarmv7le, linked
 * against ONLY libghostty-vt.a (+ tests/shims.c, -lm -lgcc), run on the
 * rooted Q10. API/usage taken verbatim from ghostty's own examples
 * (example/c-vt-formatter, example/c-vt-grid-traverse).
 *
 * Contract: feed "\x1b[31mhi"; PASS iff the plain-text formatter output
 * contains "hi" AND cell (0,0) foreground == named red (palette idx 1).
 * Prints observed values then the literal token SMOKE_OK / SMOKE_FAIL. */

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <ghostty/vt.h>

/* Embedder allocator over QNX libc malloc/free. libghostty-vt's freestanding
 * default allocator is a 0-byte FBA (the NULL path is
 * unsupported on freestanding by design) — Term49 must pass a real one, so
 * this harness does too. Manual over-align (alignment is <=16 per the C
 * contract); only malloc/free are needed (no feature macros, no posix). */
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

/* QNX libc / gcc 4.6 has no memmem — tiny substring search over a buffer. */
static int buf_has(const uint8_t *h, size_t hn, const char *n) {
    size_t nn = strlen(n);
    if (nn == 0 || hn < nn) return 0;
    for (size_t i = 0; i + nn <= hn; i++)
        if (memcmp(h + i, n, nn) == 0) return 1;
    return 0;
}

int main(void) {
    GhosttyTerminal terminal;
    GhosttyTerminalOptions opts = { .cols = 80, .rows = 24, .max_scrollback = 0 };
    if (ghostty_terminal_new(&GHA, &terminal, opts) != GHOSTTY_SUCCESS) {
        printf("SMOKE_FAIL: ghostty_terminal_new\n"); return 1;
    }

    const char *vt = "\x1b[31mhi";
    ghostty_terminal_vt_write(terminal, (const uint8_t *)vt, strlen(vt));

    /* --- text via the plain-text formatter --- */
    GhosttyFormatterTerminalOptions fo = GHOSTTY_INIT_SIZED(GhosttyFormatterTerminalOptions);
    fo.emit = GHOSTTY_FORMATTER_FORMAT_PLAIN;
    fo.trim = true;
    GhosttyFormatter fmt;
    if (ghostty_formatter_terminal_new(&GHA, &fmt, terminal, fo) != GHOSTTY_SUCCESS) {
        printf("SMOKE_FAIL: formatter_terminal_new\n"); return 1;
    }
    uint8_t *buf = NULL; size_t len = 0;
    if (ghostty_formatter_format_alloc(fmt, &GHA, &buf, &len) != GHOSTTY_SUCCESS) {
        printf("SMOKE_FAIL: formatter_format_alloc\n"); return 1;
    }
    int has_hi = (buf != NULL && buf_has(buf, len, "hi"));

    /* --- style of cell (0,0) via grid ref --- */
    GhosttyGridRef ref = GHOSTTY_INIT_SIZED(GhosttyGridRef);
    GhosttyPoint pt = {
        .tag = GHOSTTY_POINT_TAG_ACTIVE,
        .value = { .coordinate = { .x = 0, .y = 0 } },
    };
    int is_red = 0;
    GhosttyStyle style = GHOSTTY_INIT_SIZED(GhosttyStyle);
    if (ghostty_terminal_grid_ref(terminal, pt, &ref) == GHOSTTY_SUCCESS &&
        ghostty_grid_ref_style(&ref, &style) == GHOSTTY_SUCCESS) {
        is_red = (style.fg_color.tag == GHOSTTY_STYLE_COLOR_PALETTE &&
                  style.fg_color.value.palette == GHOSTTY_COLOR_NAMED_RED);
    }

    printf("text=[%.*s] len=%zu has_hi=%d fg_tag=%d fg_palette=%d is_red=%d\n",
           (int)len, buf ? (const char *)buf : "", len, has_hi,
           (int)style.fg_color.tag, (int)style.fg_color.value.palette, is_red);

    ghostty_free(&GHA, buf, len);
    ghostty_formatter_free(fmt);
    ghostty_terminal_free(terminal);

    if (has_hi && is_red) { printf("SMOKE_OK\n"); return 0; }
    printf("SMOKE_FAIL: has_hi=%d is_red=%d\n", has_hi, is_red);
    return 1;
}
