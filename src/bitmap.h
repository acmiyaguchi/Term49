/*
 * Project-owned pixel buffer. Replaces the SDL_Surface dependency that used
 * to span main.c, symmenu rendering, and the glyph cache. All composition is
 * pinned to RGBA8888 (byte order R, G, B, A) so blits are straight memcpy and
 * the framebuffer view aligns with SCREEN_FORMAT_RGBA8888.
 */

#ifndef BITMAP_H_
#define BITMAP_H_

#include <stdint.h>

#include "term_types.h"

typedef enum bitmap_fmt {
	BITMAP_FMT_RGBA8888,  /* 4 bytes / pixel, byte order R, G, B, A. */
	BITMAP_FMT_A8,        /* 1 byte / pixel, alpha mask (FreeType FT_PIXEL_MODE_GRAY). */
} bitmap_fmt_t;

typedef struct bitmap {
	uint8_t     *pixels;
	int          w;
	int          h;
	int          stride;     /* bytes per row */
	bitmap_fmt_t fmt;
	int          owns_pixels;
} bitmap_t;

bitmap_t *bitmap_alloc(int w, int h, bitmap_fmt_t fmt);
void      bitmap_free(bitmap_t *b);

/* Borrowed view over caller-managed pixels (e.g. the active screen buffer).
 * bitmap_free leaves the underlying pixels alone. */
void bitmap_view(bitmap_t *out, uint8_t *pixels, int w, int h, int stride, bitmap_fmt_t fmt);

/* Solid RGBA fill. r == NULL means the whole bitmap. RGBA8888 only. */
void bitmap_fill_rect(bitmap_t *dst, const rect_t *r, rgb_t color);

/* Copy src into dst at (dx, dy). Both must be RGBA8888. Out-of-bounds rows
 * and columns are clipped silently. */
void bitmap_blit(bitmap_t *dst, int dx, int dy, const bitmap_t *src);

#endif /* BITMAP_H_ */
