#include "symmenu_render.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unicode/utf.h>

#include "bitmap.h"
#include "font.h"
#include "io.h"
#include "prefs.h"
#include "symmenu.h"
#include "terminal.h"
#include "types.h"

void symmenu_render_destroy(symmenu_render_t *render) {
	if (render == NULL) {
		return;
	}
	bitmap_free(render->surface);
	free(render);
}

static symmenu_render_t *make_render(symmenu_t *menu, bitmap_t *surface) {
	symmenu_render_t *render = calloc(1, sizeof(*render));
	if (render == NULL) {
		bitmap_free(surface);
		return NULL;
	}
	render->menu = menu;
	render->surface = surface;
	return render;
}

symmenu_render_t *symmenu_render_create(int screen_w, int screen_h,
                                        pref_t *prefs, symmenu_t *menu) {
	int num_rows = 0;
	int longest_row_len = 0;
	for (; menu->keys[num_rows] != NULL; ++num_rows) {
		int col_len = 0;
		for (; menu->keys[num_rows][col_len].map != NULL; ++col_len) { }
		if (col_len > longest_row_len) {
			longest_row_len = col_len;
		}
	}
	if (menu->keys[0] == NULL || longest_row_len <= 0) {
		/* No rows, or the first row is non-NULL but has zero usable entries
		 * (every `map` pointer null). Either case would `/0` at bg_w below. */
		return NULL;
	}

	int bg_font_size     = preferences_guess_best_font_size(prefs, 10 * 1.25);
	int corner_font_size = bg_font_size / 5;
	int fg_font_size     = (6 * bg_font_size) / 10;

	font_t *fg_font     = font_open(prefs->font_path, fg_font_size);
	font_t *bg_font     = font_open(prefs->font_path, bg_font_size);
	font_t *corner_font = font_open(prefs->font_path, corner_font_size);
	if (fg_font == NULL || bg_font == NULL || corner_font == NULL) {
		font_close(fg_font);
		font_close(bg_font);
		font_close(corner_font);
		return NULL;
	}

	/* Sizing matches symmenu_sdl.c: one cell height = test-glyph height plus
	 * a top fret and two border lines. */
	int bg_h = font_line_skip(fg_font) + (2 * TERM_SYMKEY_BORDER_SIZE) + TERM_SYMMENU_FRET_SIZE;
	int bg_w = screen_w / longest_row_len;

	for (int row = 0; menu->keys[row] != NULL; ++row) {
		for (int col = 0; menu->keys[row][col].map != NULL; ++col) {
			symkey_t *sk = &menu->keys[row][col];
			sk->hitbox.x = col * bg_w;
			sk->hitbox.y = (screen_h - num_rows * bg_h) + row * bg_h;
			sk->hitbox.w = bg_w;
			sk->hitbox.h = bg_h;

			int to_len = strlen(sk->map->to);
			sk->uc = (UChar *)calloc(to_len + 1, sizeof(UChar));
			io_read_utf8_string(sk->map->to, to_len, sk->uc);
		}
	}

	bitmap_t *menu_surface = bitmap_alloc(screen_w, num_rows * bg_h, BITMAP_FMT_RGBA8888);
	if (menu_surface == NULL) {
		font_close(fg_font);
		font_close(bg_font);
		font_close(corner_font);
		return NULL;
	}

	rgb_t bg_color     = (rgb_t)TERM_SYMMENU_COLOR_BACKGROUND;
	rgb_t fret_color   = (rgb_t)TERM_SYMMENU_COLOR_FRET;
	rgb_t border_color = (rgb_t)TERM_SYMMENU_COLOR_BORDER;
	rgb_t font_color   = (rgb_t)TERM_SYMMENU_COLOR_FONT;

	bitmap_fill_rect(menu_surface, NULL, bg_color);

	for (int i = 0; i < num_rows; ++i) {
		rect_t r;
		r.x = 0;
		r.y = bg_h * i;
		r.w = screen_w;
		r.h = TERM_SYMMENU_FRET_SIZE;
		bitmap_fill_rect(menu_surface, &r, fret_color);

		r.y = bg_h * i + TERM_SYMMENU_FRET_SIZE;
		r.h = TERM_SYMKEY_BORDER_SIZE;
		bitmap_fill_rect(menu_surface, &r, border_color);

		r.y = bg_h * (i + 1) - TERM_SYMKEY_BORDER_SIZE;
		bitmap_fill_rect(menu_surface, &r, border_color);
	}

	UChar cornerchar[2];
	cornerchar[1] = 0;
	for (int row = 0; menu->keys[row] != NULL; ++row) {
		for (int col = 0; menu->keys[row][col].map != NULL; ++col) {
			symkey_t *sk = &menu->keys[row][col];

			int destx = sk->hitbox.x + TERM_SYMKEY_BORDER_SIZE;
			int desty = sk->hitbox.y - (screen_h - num_rows * bg_h)
			          + TERM_SYMKEY_BORDER_SIZE + TERM_SYMMENU_FRET_SIZE;
			uint32_t cp = (sk->uc != NULL) ? (uint32_t)sk->uc[0] : 0;
			bitmap_t *glyph = font_render_glyph_shaded(fg_font, cp,
			                                          FONT_STYLE_NORMAL,
			                                          font_color, bg_color);
			if (glyph != NULL) {
				bitmap_blit(menu_surface, destx, desty, glyph);
				bitmap_free(glyph);
			}

			cornerchar[0] = (UChar)sk->map->from;
			int corner_x = sk->hitbox.x;
			int corner_y = desty;
			glyph = font_render_glyph_shaded(corner_font, (uint32_t)cornerchar[0],
			                                 FONT_STYLE_NORMAL,
			                                 font_color, bg_color);
			if (glyph != NULL) {
				bitmap_blit(menu_surface, corner_x, corner_y, glyph);
				bitmap_free(glyph);
			}
		}
	}

	font_close(fg_font);
	font_close(bg_font);
	font_close(corner_font);

	return make_render(menu, menu_surface);
}
