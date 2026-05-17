#include <stdlib.h>

#include "renderer.h"
#include "renderer_sdl.h"
#include "symmenu_sdl.h"

struct renderer_sdl {
	symmenu_sdl_render_t *main_symmenu;
	symmenu_sdl_render_t *accent_menus[26][2];
};

renderer_sdl_t *renderer_sdl_create(void) {
	return calloc(1, sizeof(renderer_sdl_t));
}

static void renderer_sdl_destroy_symmenus(renderer_sdl_t *renderer) {
	if (renderer == NULL) {
		return;
	}

	symmenu_sdl_destroy_render(renderer->main_symmenu);
	renderer->main_symmenu = NULL;
	for (char c = 'a'; c <= 'z'; ++c) {
		size_t idx = (size_t)(c - 'a');
		for (int uppercase = 0; uppercase < 2; ++uppercase) {
			symmenu_sdl_destroy_render(renderer->accent_menus[idx][uppercase]);
			renderer->accent_menus[idx][uppercase] = NULL;
		}
	}
}

void renderer_sdl_destroy(renderer_sdl_t *renderer) {
	if (renderer == NULL) {
		return;
	}
	renderer_sdl_destroy_symmenus(renderer);
	free(renderer);
}

int renderer_sdl_init_symmenus(renderer_sdl_t *renderer, SDL_Surface *screen, pref_t *prefs) {
	if (renderer == NULL || screen == NULL || prefs == NULL) {
		return -1;
	}

	renderer_sdl_destroy_symmenus(renderer);

	renderer->main_symmenu = symmenu_sdl_render(screen, prefs, prefs->main_symmenu);
	if (renderer->main_symmenu == NULL) {
		return -1;
	}

	for (char c = 'a'; c <= 'z'; ++c) {
		size_t idx = (size_t)(c - 'a');

		// lowercase
		symmenu_t *m = prefs->accent_menus[idx][0];
		if (m->entries[1].to != NULL) {
			renderer->accent_menus[idx][0] = symmenu_sdl_render(screen, prefs, m);
			if (renderer->accent_menus[idx][0] == NULL) {
				return -1;
			}
		}

		// uppercase
		m = prefs->accent_menus[idx][1];
		if (m->entries[1].to != NULL) {
			renderer->accent_menus[idx][1] = symmenu_sdl_render(screen, prefs, m);
			if (renderer->accent_menus[idx][1] == NULL) {
				return -1;
			}
		}
	}

	return 0;
}

static symmenu_sdl_render_t *renderer_sdl_render_for_symmenu(renderer_sdl_t *renderer, symmenu_t *menu) {
	if (renderer == NULL || menu == NULL) {
		return NULL;
	}
	if (renderer->main_symmenu != NULL && renderer->main_symmenu->menu == menu) {
		return renderer->main_symmenu;
	}
	for (char c = 'a'; c <= 'z'; ++c) {
		size_t idx = (size_t)(c - 'a');
		for (int uppercase = 0; uppercase < 2; ++uppercase) {
			symmenu_sdl_render_t *render = renderer->accent_menus[idx][uppercase];
			if (render != NULL && render->menu == menu) {
				return render;
			}
		}
	}
	return NULL;
}

SDL_Surface *renderer_sdl_symmenu_surface_for(renderer_sdl_t *renderer, symmenu_t *menu) {
	symmenu_sdl_render_t *render = renderer_sdl_render_for_symmenu(renderer, menu);
	return render != NULL ? render->surface : NULL;
}

int renderer_sdl_symmenu_height(renderer_sdl_t *renderer, symmenu_t *menu) {
	SDL_Surface *surface = renderer_sdl_symmenu_surface_for(renderer, menu);
	return surface != NULL ? surface->h : 0;
}

/* --- renderer vtable adapter (the seam #6 swaps) --- */

static int sdl_ops_init_symmenus(renderer_t *r, void *screen, pref_t *prefs) {
	return renderer_sdl_init_symmenus(renderer_impl(r), (SDL_Surface *)screen, prefs);
}

static void *sdl_ops_symmenu_surface_for(renderer_t *r, symmenu_t *menu) {
	return renderer_sdl_symmenu_surface_for(renderer_impl(r), menu);
}

static int sdl_ops_symmenu_height(renderer_t *r, symmenu_t *menu) {
	return renderer_sdl_symmenu_height(renderer_impl(r), menu);
}

static void sdl_ops_destroy(renderer_t *r) {
	renderer_sdl_destroy(renderer_impl(r));
}

static const renderer_ops_t SDL_RENDERER_OPS = {
	sdl_ops_init_symmenus,
	sdl_ops_symmenu_surface_for,
	sdl_ops_symmenu_height,
	sdl_ops_destroy,
};

const renderer_ops_t *renderer_sdl_ops(void) {
	return &SDL_RENDERER_OPS;
}

renderer_t *renderer_sdl_new(void) {
	renderer_t *r;
	renderer_sdl_t *impl;

	r = renderer_create(renderer_sdl_ops());
	if (r == NULL) {
		return NULL;
	}

	impl = renderer_sdl_create();
	if (impl == NULL) {
		renderer_destroy(r);   /* sdl_ops_destroy(NULL impl) is a no-op */
		return NULL;
	}

	renderer_set_impl(r, impl);
	return r;
}
