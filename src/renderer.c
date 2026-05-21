/*
 * Backend-agnostic renderer wrapper. Holds the vtable + the backend's
 * opaque impl pointer and forwards each operation.
 */

#include <stdlib.h>

#include "renderer.h"

struct renderer {
	const renderer_ops_t *ops;
	void *impl;
};

renderer_t *renderer_create(const renderer_ops_t *ops) {
	renderer_t *r;
	if (ops == NULL) {
		return NULL;
	}
	r = calloc(1, sizeof(*r));
	if (r == NULL) {
		return NULL;
	}
	r->ops = ops;
	return r;
}

void renderer_set_impl(renderer_t *r, void *impl) {
	if (r != NULL) {
		r->impl = impl;
	}
}

void *renderer_impl(renderer_t *r) {
	return r != NULL ? r->impl : NULL;
}

int renderer_init_symmenus(renderer_t *r, pref_t *prefs) {
	if (r == NULL || r->ops->init_symmenus == NULL) {
		return -1;
	}
	return r->ops->init_symmenus(r, prefs);
}

void renderer_set_font(renderer_t *r, font_t *font) {
	if (r != NULL && r->ops->set_font != NULL) {
		r->ops->set_font(r, font);
	}
}

int renderer_framebuffer_size(renderer_t *r, int *w, int *h) {
	if (r == NULL || r->ops->framebuffer_size == NULL) {
		return -1;
	}
	return r->ops->framebuffer_size(r, w, h);
}

int renderer_begin_frame(renderer_t *r) {
	if (r == NULL || r->ops->begin_frame == NULL) {
		return 0;
	}
	return r->ops->begin_frame(r);
}

void renderer_end_frame(renderer_t *r, int was_full) {
	if (r != NULL && r->ops->end_frame != NULL) {
		r->ops->end_frame(r, was_full);
	}
}

void renderer_clear(renderer_t *r, rgb_t color) {
	if (r != NULL && r->ops->clear != NULL) {
		r->ops->clear(r, color);
	}
}

void renderer_fill_rect(renderer_t *r, const rect_t *dst, rgb_t color) {
	if (r != NULL && r->ops->fill_rect != NULL) {
		r->ops->fill_rect(r, dst, color);
	}
}

int renderer_draw_glyph(renderer_t *r, int x, int y,
                        uint32_t codepoint, font_style_t style,
                        rgb_t fg, rgb_t bg) {
	if (r == NULL || r->ops->draw_glyph == NULL) {
		return -1;
	}
	return r->ops->draw_glyph(r, x, y, codepoint, style, fg, bg);
}

void renderer_draw_bitmap(renderer_t *r, int x, int y, const bitmap_t *src) {
	if (r != NULL && r->ops->draw_bitmap != NULL) {
		r->ops->draw_bitmap(r, x, y, src);
	}
}

const bitmap_t *renderer_symmenu_surface_for(renderer_t *r, symmenu_t *menu) {
	if (r == NULL || r->ops->symmenu_surface_for == NULL) {
		return NULL;
	}
	return r->ops->symmenu_surface_for(r, menu);
}

int renderer_symmenu_height(renderer_t *r, symmenu_t *menu) {
	if (r == NULL || r->ops->symmenu_height == NULL) {
		return 0;
	}
	return r->ops->symmenu_height(r, menu);
}

void renderer_destroy(renderer_t *r) {
	if (r == NULL) {
		return;
	}
	if (r->ops->destroy != NULL) {
		r->ops->destroy(r);
	}
	free(r);
}
