/*
 * FreeType-only glyph rasterizer. Replaces the vendored src/SDL_ttf.c when
 * #6 (SDL removal) lands. Public surface mirrors the TTF subset Term50
 * actually uses (TTF_OpenFont, TTF_CloseFont, TTF_FontLineSkip,
 * TTF_FontFaceIsFixedWidth, TTF_GlyphMetrics, TTF_RenderUNICODE_Shaded).
 */

#ifndef FONT_H_
#define FONT_H_

#include <stdint.h>

#include "bitmap.h"
#include "term_types.h"

typedef enum font_style {
	FONT_STYLE_NORMAL    = 0,
	FONT_STYLE_BOLD      = 1 << 0,
	FONT_STYLE_ITALIC    = 1 << 1,
	FONT_STYLE_UNDERLINE = 1 << 2,
} font_style_t;

typedef struct font font_t;

int  font_library_init(void);
void font_library_quit(void);

font_t *font_open(const char *path, int pt_size);
void    font_close(font_t *f);

/* Pixel advance for a line of text. Mirrors TTF_FontLineSkip. */
int font_line_skip(const font_t *f);

/* 1 if monospaced. Mirrors TTF_FontFaceIsFixedWidth. */
int font_is_fixed_width(const font_t *f);

/* Mirrors TTF_GlyphMetrics. Out params may be NULL. Returns 0 on success,
 * -1 if the codepoint has no glyph. */
int font_glyph_metrics(font_t *f, uint32_t codepoint,
                       int *minx, int *maxx,
                       int *miny, int *maxy,
                       int *advance);

/* Rasterize codepoint into a freshly-allocated RGBA8888 bitmap of size
 * (advance, line_skip). Every output pixel = mix(bg, fg, alpha/255).
 * Matches TTF_RenderUNICODE_Shaded so callers blit raw -- no extra fill. */
bitmap_t *font_render_glyph_shaded(font_t *f, uint32_t codepoint,
                                   font_style_t style,
                                   rgb_t fg, rgb_t bg);

#endif /* FONT_H_ */
