#include "renderer_screen.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <screen/screen.h>

#include "bitmap.h"
#include "font.h"
#include "platform.h"
#include "platform_screen.h"
#include "prefs.h"
#include "renderer.h"
#include "symmenu.h"
#include "symmenu_render.h"
#include "term_types.h"

#define GLYPH_CACHE_SIZE 2048

typedef struct glyph_cache_entry {
	uint32_t     codepoint;
	uint32_t     fg_key;
	uint32_t     bg_key;
	font_style_t style;
	bitmap_t    *bm;
} glyph_cache_entry_t;

typedef struct glyph_cache {
	glyph_cache_entry_t entries[GLYPH_CACHE_SIZE];
} glyph_cache_t;

typedef struct renderer_screen {
	platform_t       *plat;        /* borrowed */
	font_t           *font;        /* borrowed */
	screen_context_t  ctx;
	screen_window_t   window;
	bitmap_t          fb;          /* view over the active buffer */
	int               fb_valid;
	int               fb_w;
	int               fb_h;
	int               buffer_count;
	screen_buffer_t   active_buffer;
	glyph_cache_t     cache;
	symmenu_render_t *main_symmenu;
	symmenu_render_t *accent_menus[26][2];
} renderer_screen_t;

static renderer_screen_t *self_of(renderer_t *r) {
	return (renderer_screen_t *)renderer_impl(r);
}

static uint32_t color_key(rgb_t c) {
	return ((uint32_t)c.r << 16) | ((uint32_t)c.g << 8) | (uint32_t)c.b;
}

static unsigned glyph_hash(uint32_t codepoint, font_style_t style, uint32_t fg, uint32_t bg) {
	uint32_t h = codepoint * 2654435761u;
	h ^= fg * 2246822519u;
	h ^= bg * 3266489917u;
	h ^= (uint32_t)style * 668265263u;
	return (unsigned)(h & (GLYPH_CACHE_SIZE - 1));
}

static void glyph_cache_clear(glyph_cache_t *c) {
	for (int i = 0; i < GLYPH_CACHE_SIZE; ++i) {
		if (c->entries[i].bm != NULL) {
			bitmap_free(c->entries[i].bm);
			c->entries[i].bm = NULL;
		}
	}
}

static const bitmap_t *glyph_cache_lookup(renderer_screen_t *self,
                                          uint32_t codepoint, font_style_t style,
                                          rgb_t fg, rgb_t bg) {
	uint32_t fg_key = color_key(fg);
	uint32_t bg_key = color_key(bg);
	unsigned idx = glyph_hash(codepoint, style, fg_key, bg_key);
	glyph_cache_entry_t *entry = &self->cache.entries[idx];

	if (entry->bm != NULL && entry->codepoint == codepoint &&
	    entry->style == style && entry->fg_key == fg_key && entry->bg_key == bg_key) {
		return entry->bm;
	}

	if (entry->bm != NULL) {
		bitmap_free(entry->bm);
		entry->bm = NULL;
	}
	entry->bm = font_render_glyph_shaded(self->font, codepoint, style, fg, bg);
	if (entry->bm == NULL) {
		return NULL;
	}
	entry->codepoint = codepoint;
	entry->style     = style;
	entry->fg_key    = fg_key;
	entry->bg_key    = bg_key;
	return entry->bm;
}

static int latch_framebuffer(renderer_screen_t *self) {
	/* RENDER_BUFFERS is an array sized by the buffer count set with
	 * screen_create_window_buffers(). For double-buffered windows we MUST
	 * provide storage for both pointers or screen_get_window_property_pv
	 * will overrun a single-pointer local. We render to the first entry
	 * (the next buffer ready for the application) and post that same
	 * entry in end_frame. */
	int count = self->buffer_count > 0 ? self->buffer_count : 2;
	screen_buffer_t buffers[4] = {NULL, NULL, NULL, NULL};
	if (count > (int)(sizeof(buffers) / sizeof(buffers[0]))) {
		count = (int)(sizeof(buffers) / sizeof(buffers[0]));
	}
	if (screen_get_window_property_pv(self->window, SCREEN_PROPERTY_RENDER_BUFFERS,
	                                  (void **)buffers) != 0 || buffers[0] == NULL) {
		self->fb_valid = 0;
		self->active_buffer = NULL;
		return -1;
	}
	screen_buffer_t buffer = buffers[0];
	void *ptr = NULL;
	int   stride = 0;
	int   size[2] = {0, 0};
	if (screen_get_buffer_property_pv(buffer, SCREEN_PROPERTY_POINTER, &ptr) != 0 ||
	    screen_get_buffer_property_iv(buffer, SCREEN_PROPERTY_STRIDE,  &stride) != 0 ||
	    screen_get_buffer_property_iv(buffer, SCREEN_PROPERTY_BUFFER_SIZE, size) != 0 ||
	    ptr == NULL || stride <= 0 || size[0] <= 0 || size[1] <= 0) {
		self->fb_valid = 0;
		self->active_buffer = NULL;
		return -1;
	}
	bitmap_view(&self->fb, (uint8_t *)ptr, size[0], size[1], stride, BITMAP_FMT_RGBA8888);
	self->fb_valid = 1;
	self->fb_w = size[0];
	self->fb_h = size[1];
	self->active_buffer = buffer;
	return 0;
}

static int screen_framebuffer_size(renderer_t *r, int *w, int *h) {
	renderer_screen_t *self = self_of(r);
	if (self == NULL) {
		return -1;
	}
	if (!self->fb_valid && latch_framebuffer(self) != 0) {
		int size[2] = {0, 0};
		if (screen_get_window_property_iv(self->window, SCREEN_PROPERTY_SIZE, size) != 0) {
			return -1;
		}
		/* Fail closed: callers (rescreen, symmenu init) divide by these
		 * dimensions, so a "successful" zero return turns into a SIGFPE
		 * downstream. */
		if (size[0] <= 0 || size[1] <= 0) {
			return -1;
		}
		if (w) *w = size[0];
		if (h) *h = size[1];
		return 0;
	}
	if (w) *w = self->fb_w;
	if (h) *h = self->fb_h;
	return 0;
}

static void screen_begin_frame(renderer_t *r) {
	renderer_screen_t *self = self_of(r);
	if (self == NULL) {
		return;
	}
	latch_framebuffer(self);
}

static void screen_end_frame(renderer_t *r) {
	renderer_screen_t *self = self_of(r);
	if (self == NULL || !self->fb_valid || self->active_buffer == NULL) {
		return;
	}
	int rect[4] = {0, 0, self->fb_w, self->fb_h};
	screen_post_window(self->window, self->active_buffer, 1, rect, 0);
	self->fb_valid = 0;
	self->active_buffer = NULL;
}

static void screen_clear(renderer_t *r, rgb_t color) {
	renderer_screen_t *self = self_of(r);
	if (self == NULL || !self->fb_valid) {
		return;
	}
	bitmap_fill_rect(&self->fb, NULL, color);
}

static void screen_fill_rect(renderer_t *r, const rect_t *dst, rgb_t color) {
	renderer_screen_t *self = self_of(r);
	if (self == NULL || !self->fb_valid) {
		return;
	}
	bitmap_fill_rect(&self->fb, dst, color);
}

static int screen_draw_glyph(renderer_t *r, int x, int y,
                              uint32_t codepoint, font_style_t style,
                              rgb_t fg, rgb_t bg) {
	renderer_screen_t *self = self_of(r);
	if (self == NULL || !self->fb_valid || self->font == NULL) {
		return -1;
	}
	const bitmap_t *bm = glyph_cache_lookup(self, codepoint, style, fg, bg);
	if (bm == NULL) {
		return -1;
	}
	bitmap_blit(&self->fb, x, y, bm);
	return 0;
}

static void screen_draw_bitmap(renderer_t *r, int x, int y, const bitmap_t *src) {
	renderer_screen_t *self = self_of(r);
	if (self == NULL || !self->fb_valid || src == NULL) {
		return;
	}
	bitmap_blit(&self->fb, x, y, src);
}

static void screen_set_font(renderer_t *r, font_t *font) {
	renderer_screen_t *self = self_of(r);
	if (self == NULL) {
		return;
	}
	if (self->font != font) {
		glyph_cache_clear(&self->cache);
	}
	self->font = font;
}

static void destroy_symmenus(renderer_screen_t *self) {
	if (self == NULL) {
		return;
	}
	symmenu_render_destroy(self->main_symmenu);
	self->main_symmenu = NULL;
	for (char c = 'a'; c <= 'z'; ++c) {
		size_t idx = (size_t)(c - 'a');
		for (int uppercase = 0; uppercase < 2; ++uppercase) {
			symmenu_render_destroy(self->accent_menus[idx][uppercase]);
			self->accent_menus[idx][uppercase] = NULL;
		}
	}
}

static int screen_init_symmenus(renderer_t *r, pref_t *prefs) {
	renderer_screen_t *self = self_of(r);
	if (self == NULL || prefs == NULL) {
		return -1;
	}

	int w = 0, h = 0;
	if (screen_framebuffer_size(r, &w, &h) != 0 || w <= 0 || h <= 0) {
		return -1;
	}

	destroy_symmenus(self);

	self->main_symmenu = symmenu_render_create(w, h, prefs, prefs->main_symmenu);
	if (self->main_symmenu == NULL) {
		return -1;
	}
	for (char c = 'a'; c <= 'z'; ++c) {
		size_t idx = (size_t)(c - 'a');
		symmenu_t *m = prefs->accent_menus[idx][0];
		if (m->entries[1].to != NULL) {
			self->accent_menus[idx][0] = symmenu_render_create(w, h, prefs, m);
			if (self->accent_menus[idx][0] == NULL) {
				return -1;
			}
		}
		m = prefs->accent_menus[idx][1];
		if (m->entries[1].to != NULL) {
			self->accent_menus[idx][1] = symmenu_render_create(w, h, prefs, m);
			if (self->accent_menus[idx][1] == NULL) {
				return -1;
			}
		}
	}
	return 0;
}

static symmenu_render_t *find_symmenu_render(renderer_screen_t *self, symmenu_t *menu) {
	if (self == NULL || menu == NULL) {
		return NULL;
	}
	if (self->main_symmenu != NULL && self->main_symmenu->menu == menu) {
		return self->main_symmenu;
	}
	for (char c = 'a'; c <= 'z'; ++c) {
		size_t idx = (size_t)(c - 'a');
		for (int uppercase = 0; uppercase < 2; ++uppercase) {
			symmenu_render_t *render = self->accent_menus[idx][uppercase];
			if (render != NULL && render->menu == menu) {
				return render;
			}
		}
	}
	return NULL;
}

static const bitmap_t *screen_symmenu_surface_for(renderer_t *r, symmenu_t *menu) {
	symmenu_render_t *render = find_symmenu_render(self_of(r), menu);
	return render != NULL ? render->surface : NULL;
}

static int screen_symmenu_height(renderer_t *r, symmenu_t *menu) {
	symmenu_render_t *render = find_symmenu_render(self_of(r), menu);
	return render != NULL && render->surface != NULL ? render->surface->h : 0;
}

static void screen_destroy(renderer_t *r) {
	renderer_screen_t *self = self_of(r);
	if (self == NULL) {
		return;
	}
	destroy_symmenus(self);
	glyph_cache_clear(&self->cache);
	free(self);
}

static const renderer_ops_t SCREEN_RENDERER_OPS = {
	.init_symmenus       = screen_init_symmenus,
	.destroy             = screen_destroy,
	.set_font            = screen_set_font,
	.framebuffer_size    = screen_framebuffer_size,
	.begin_frame         = screen_begin_frame,
	.end_frame           = screen_end_frame,
	.clear               = screen_clear,
	.fill_rect           = screen_fill_rect,
	.draw_glyph          = screen_draw_glyph,
	.draw_bitmap         = screen_draw_bitmap,
	.symmenu_surface_for = screen_symmenu_surface_for,
	.symmenu_height      = screen_symmenu_height,
};

const renderer_ops_t *renderer_screen_ops(void) {
	return &SCREEN_RENDERER_OPS;
}

renderer_t *renderer_screen_create(platform_t *platform, font_t *font) {
	if (platform == NULL) {
		return NULL;
	}
	screen_context_t ctx = platform_screen_context(platform);
	screen_window_t  win = platform_screen_window(platform);
	if (ctx == NULL || win == NULL) {
		return NULL;
	}

	renderer_screen_t *self = calloc(1, sizeof(*self));
	if (self == NULL) {
		return NULL;
	}
	self->plat   = platform;
	self->font   = font;
	self->ctx    = ctx;
	self->window = win;

	int bcount = 0;
	if (screen_get_window_property_iv(win, SCREEN_PROPERTY_RENDER_BUFFER_COUNT, &bcount) != 0 || bcount <= 0) {
		bcount = 2;
	}
	self->buffer_count = bcount;

	renderer_t *r = renderer_create(renderer_screen_ops());
	if (r == NULL) {
		free(self);
		return NULL;
	}
	renderer_set_impl(r, self);
	return r;
}
