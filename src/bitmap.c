#include "bitmap.h"

#include <stdlib.h>
#include <string.h>

static int fmt_bpp(bitmap_fmt_t fmt) {
	return fmt == BITMAP_FMT_RGBA8888 ? 4 : 1;
}

bitmap_t *bitmap_alloc(int w, int h, bitmap_fmt_t fmt) {
	if (w <= 0 || h <= 0) return NULL;
	bitmap_t *b = calloc(1, sizeof(*b));
	if (!b) return NULL;
	int bpp = fmt_bpp(fmt);
	b->w = w;
	b->h = h;
	b->stride = w * bpp;
	b->fmt = fmt;
	b->owns_pixels = 1;
	b->pixels = calloc((size_t)h, (size_t)b->stride);
	if (!b->pixels) {
		free(b);
		return NULL;
	}
	return b;
}

void bitmap_free(bitmap_t *b) {
	if (!b) return;
	if (b->owns_pixels) free(b->pixels);
	free(b);
}

void bitmap_view(bitmap_t *out, uint8_t *pixels, int w, int h, int stride, bitmap_fmt_t fmt) {
	out->pixels = pixels;
	out->w = w;
	out->h = h;
	out->stride = stride;
	out->fmt = fmt;
	out->owns_pixels = 0;
}

void bitmap_fill_rect(bitmap_t *dst, const rect_t *r, rgb_t color) {
	if (!dst || dst->fmt != BITMAP_FMT_RGBA8888 || !dst->pixels) return;
	int x0 = 0, y0 = 0, x1 = dst->w, y1 = dst->h;
	if (r) {
		if (r->x > x0) x0 = r->x;
		if (r->y > y0) y0 = r->y;
		if (r->x + r->w < x1) x1 = r->x + r->w;
		if (r->y + r->h < y1) y1 = r->y + r->h;
	}
	if (x0 >= x1 || y0 >= y1) return;
	for (int y = y0; y < y1; ++y) {
		uint8_t *row = dst->pixels + (size_t)y * dst->stride + (size_t)x0 * 4;
		for (int x = x0; x < x1; ++x) {
			row[0] = color.b;
			row[1] = color.g;
			row[2] = color.r;
			row[3] = 0xFF;
			row += 4;
		}
	}
}

void bitmap_blit(bitmap_t *dst, int dx, int dy, const bitmap_t *src) {
	if (!dst || !src) return;
	if (dst->fmt != BITMAP_FMT_RGBA8888 || src->fmt != BITMAP_FMT_RGBA8888) return;
	int sx0 = 0, sy0 = 0, sx1 = src->w, sy1 = src->h;
	if (dx < 0) { sx0 -= dx; dx = 0; }
	if (dy < 0) { sy0 -= dy; dy = 0; }
	if (dx + (sx1 - sx0) > dst->w) sx1 = sx0 + (dst->w - dx);
	if (dy + (sy1 - sy0) > dst->h) sy1 = sy0 + (dst->h - dy);
	if (sx0 >= sx1 || sy0 >= sy1) return;
	size_t row_bytes = (size_t)(sx1 - sx0) * 4;
	for (int y = sy0; y < sy1; ++y) {
		const uint8_t *srow = src->pixels + (size_t)y * src->stride + (size_t)sx0 * 4;
		uint8_t       *drow = dst->pixels + (size_t)(dy + (y - sy0)) * dst->stride + (size_t)dx * 4;
		memcpy(drow, srow, row_bytes);
	}
}
