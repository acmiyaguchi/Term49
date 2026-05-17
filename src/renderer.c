/*
 * Backend-agnostic renderer wrapper. Holds the vtable + the backend's
 * opaque impl pointer and forwards each operation. The concrete backend
 * (renderer_sdl.c today; renderer_screen.c in #6) only implements the ops
 * and a factory; no caller dereferences this struct.
 */

#include <stdlib.h>

#include "renderer.h"

struct t49_renderer {
	const t49_renderer_ops_t *ops;
	void *impl;
};

t49_renderer_t *renderer_create(const t49_renderer_ops_t *ops) {
	t49_renderer_t *r;
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

void renderer_set_impl(t49_renderer_t *r, void *impl) {
	if (r != NULL) {
		r->impl = impl;
	}
}

void *renderer_impl(t49_renderer_t *r) {
	return r != NULL ? r->impl : NULL;
}

int renderer_init_symmenus(t49_renderer_t *r, void *screen, pref_t *prefs) {
	if (r == NULL || r->ops->init_symmenus == NULL) {
		return -1;
	}
	return r->ops->init_symmenus(r, screen, prefs);
}

void *renderer_symmenu_surface_for(t49_renderer_t *r, symmenu_t *menu) {
	if (r == NULL || r->ops->symmenu_surface_for == NULL) {
		return NULL;
	}
	return r->ops->symmenu_surface_for(r, menu);
}

int renderer_symmenu_height(t49_renderer_t *r, symmenu_t *menu) {
	if (r == NULL || r->ops->symmenu_height == NULL) {
		return 0;
	}
	return r->ops->symmenu_height(r, menu);
}

void renderer_destroy(t49_renderer_t *r) {
	if (r == NULL) {
		return;
	}
	if (r->ops->destroy != NULL) {
		r->ops->destroy(r);
	}
	free(r);
}
