#include "font.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H
#include FT_OUTLINE_H
#include FT_SYNTHESIS_H

struct font {
	FT_Face face;
	int     pt_size;
};

static FT_Library g_library;
static int        g_library_initialized;

int font_library_init(void) {
	if (g_library_initialized) return 0;
	if (FT_Init_FreeType(&g_library) != 0) return -1;
	g_library_initialized = 1;
	return 0;
}

void font_library_quit(void) {
	if (!g_library_initialized) return;
	FT_Done_FreeType(g_library);
	g_library = NULL;
	g_library_initialized = 0;
}

font_t *font_open(const char *path, int pt_size) {
	if (!g_library_initialized || !path || pt_size <= 0) return NULL;
	font_t *f = calloc(1, sizeof(*f));
	if (!f) return NULL;
	if (FT_New_Face(g_library, path, 0, &f->face) != 0) {
		free(f);
		return NULL;
	}
	/* Matches SDL_ttf's TTF_OpenFont, which calls
	 * FT_Set_Char_Size(face, 0, ptsize * 64, 0, 0). */
	if (FT_Set_Char_Size(f->face, 0, pt_size * 64, 0, 0) != 0) {
		FT_Done_Face(f->face);
		free(f);
		return NULL;
	}
	f->pt_size = pt_size;
	return f;
}

void font_close(font_t *f) {
	if (!f) return;
	if (f->face) FT_Done_Face(f->face);
	free(f);
}

int font_line_skip(const font_t *f) {
	if (!f || !f->face) return 0;
	return (int)(f->face->size->metrics.height >> 6);
}

int font_is_fixed_width(const font_t *f) {
	if (!f || !f->face) return 0;
	return (f->face->face_flags & FT_FACE_FLAG_FIXED_WIDTH) ? 1 : 0;
}

int font_glyph_metrics(font_t *f, uint32_t codepoint,
                       int *minx, int *maxx,
                       int *miny, int *maxy,
                       int *advance) {
	if (!f || !f->face) return -1;
	FT_UInt gi = FT_Get_Char_Index(f->face, codepoint);
	if (gi == 0) return -1;
	if (FT_Load_Glyph(f->face, gi, FT_LOAD_DEFAULT) != 0) return -1;
	const FT_Glyph_Metrics *m = &f->face->glyph->metrics;
	if (minx)    *minx    = (int)(m->horiBearingX >> 6);
	if (maxx)    *maxx    = (int)((m->horiBearingX + m->width) >> 6);
	if (maxy)    *maxy    = (int)(m->horiBearingY >> 6);
	if (miny)    *miny    = (int)((m->horiBearingY - m->height) >> 6);
	if (advance) *advance = (int)(m->horiAdvance >> 6);
	return 0;
}

static void apply_italic_transform(font_style_t style, FT_Face face) {
	if (style & FONT_STYLE_ITALIC) {
		FT_Matrix shear;
		shear.xx = 1 << 16;
		shear.xy = (FT_Fixed)(0.2 * (1 << 16));
		shear.yx = 0;
		shear.yy = 1 << 16;
		FT_Set_Transform(face, &shear, NULL);
	} else {
		FT_Set_Transform(face, NULL, NULL);
	}
}

bitmap_t *font_render_glyph_shaded(font_t *f, uint32_t codepoint,
                                   font_style_t style,
                                   rgb_t fg, rgb_t bg) {
	if (!f || !f->face) return NULL;
	FT_Face face = f->face;

	apply_italic_transform(style, face);
	if (FT_Load_Char(face, codepoint, FT_LOAD_DEFAULT) != 0) return NULL;
	if (style & FONT_STYLE_BOLD) FT_GlyphSlot_Embolden(face->glyph);
	if (FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL) != 0) return NULL;

	FT_GlyphSlot slot      = face->glyph;
	int          ascender  = (int)(face->size->metrics.ascender >> 6);
	int          line_skip = (int)(face->size->metrics.height >> 6);
	int          advance   = (int)(slot->advance.x >> 6);
	if (line_skip <= 0) line_skip = 1;
	if (advance   <= 0) advance   = 1;

	bitmap_t *out = bitmap_alloc(advance, line_skip, BITMAP_FMT_RGBA8888);
	if (!out) return NULL;
	bitmap_fill_rect(out, NULL, bg);

	if (slot->bitmap.buffer && slot->bitmap.pixel_mode == FT_PIXEL_MODE_GRAY) {
		int gx0 = slot->bitmap_left;
		int gy0 = ascender - slot->bitmap_top;
		int gw  = (int)slot->bitmap.width;
		int gh  = (int)slot->bitmap.rows;
		int pitch = slot->bitmap.pitch;
		for (int y = 0; y < gh; ++y) {
			int dy = gy0 + y;
			if (dy < 0 || dy >= out->h) continue;
			const uint8_t *srow = slot->bitmap.buffer + (size_t)y * pitch;
			uint8_t       *drow = out->pixels + (size_t)dy * out->stride;
			for (int x = 0; x < gw; ++x) {
				int dx = gx0 + x;
				if (dx < 0 || dx >= out->w) continue;
				uint8_t a = srow[x];
				uint8_t *p = drow + (size_t)dx * 4;
				p[0] = (uint8_t)((fg.r * a + bg.r * (255 - a)) / 255);
				p[1] = (uint8_t)((fg.g * a + bg.g * (255 - a)) / 255);
				p[2] = (uint8_t)((fg.b * a + bg.b * (255 - a)) / 255);
				p[3] = 0xFF;
			}
		}
	}

	if (style & FONT_STYLE_UNDERLINE) {
		int ul_thick = f->pt_size / 12;
		if (ul_thick < 1) ul_thick = 1;
		int ul_y = ascender + 1;
		if (ul_y + ul_thick > out->h) ul_y = out->h - ul_thick;
		if (ul_y < 0) ul_y = 0;
		rect_t r;
		r.x = 0;
		r.y = ul_y;
		r.w = out->w;
		r.h = ul_thick;
		bitmap_fill_rect(out, &r, fg);
	}

	return out;
}
