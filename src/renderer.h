#ifndef RENDERER_H_
#define RENDERER_H_

#include <stdint.h>

#include "bitmap.h"
#include "font.h"
#include "platform.h"
#include "prefs.h"
#include "symmenu.h"
#include "term_types.h"

/* Backend-agnostic renderer handle + operations. Owns the framebuffer
 * present path, a glyph cache, and the symmenu surface cache. The widened
 * vtable lets main.c drive a frame without referencing any windowing or
 * font SDK directly. */
typedef struct renderer renderer_t;

typedef struct renderer_ops {
	/* lifecycle */
	int  (*init_symmenus)(renderer_t *r, pref_t *prefs);
	void (*destroy)(renderer_t *r);
	void (*set_font)(renderer_t *r, font_t *font);

	/* frame composition. begin_frame returns 1 when the latched buffer is
	 * stale (last fully painted before the buffers diverged), so the caller
	 * must paint a full frame this iteration. end_frame's was_full flag
	 * lets the backend mark the just-painted buffer fresh and any others
	 * stale, which keeps partial paints safe once both buffers are caught
	 * up. */
	int  (*framebuffer_size)(renderer_t *r, int *w, int *h);
	int  (*begin_frame)(renderer_t *r);
	void (*end_frame)(renderer_t *r, int was_full);
	void (*clear)(renderer_t *r, rgb_t color);
	void (*fill_rect)(renderer_t *r, const rect_t *dst, rgb_t color);
	int  (*draw_glyph)(renderer_t *r, int x, int y,
	                   uint32_t codepoint, font_style_t style,
	                   rgb_t fg, rgb_t bg);
	void (*draw_bitmap)(renderer_t *r, int x, int y, const bitmap_t *src);

	/* symmenu accessors */
	const bitmap_t *(*symmenu_surface_for)(renderer_t *r, symmenu_t *menu);
	int             (*symmenu_height)(renderer_t *r, symmenu_t *menu);
} renderer_ops_t;

/* Generic wrapper lifecycle (renderer.c). */
renderer_t *renderer_create(const renderer_ops_t *ops);
void  renderer_set_impl(renderer_t *r, void *impl);
void *renderer_impl(renderer_t *r);

int   renderer_init_symmenus(renderer_t *r, pref_t *prefs);
void  renderer_set_font(renderer_t *r, font_t *font);
int   renderer_framebuffer_size(renderer_t *r, int *w, int *h);
int   renderer_begin_frame(renderer_t *r);
void  renderer_end_frame(renderer_t *r, int was_full);
void  renderer_clear(renderer_t *r, rgb_t color);
void  renderer_fill_rect(renderer_t *r, const rect_t *dst, rgb_t color);
int   renderer_draw_glyph(renderer_t *r, int x, int y,
                          uint32_t codepoint, font_style_t style,
                          rgb_t fg, rgb_t bg);
void  renderer_draw_bitmap(renderer_t *r, int x, int y, const bitmap_t *src);
const bitmap_t *renderer_symmenu_surface_for(renderer_t *r, symmenu_t *menu);
int   renderer_symmenu_height(renderer_t *r, symmenu_t *menu);
void  renderer_destroy(renderer_t *r);

/* Native Screen backend factory (renderer_screen.c). */
renderer_t *renderer_screen_create(platform_t *platform, font_t *font);
const renderer_ops_t *renderer_screen_ops(void);

#endif
