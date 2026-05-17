#ifndef RENDERER_H_
#define RENDERER_H_

#include "prefs.h"
#include "symmenu.h"

/* Backend-agnostic renderer handle + operations. Concrete backends own
 * windows, surfaces/buffers, fonts, glyph caches, and render caches. SDL
 * surfaces cross this boundary as void* so the header stays backend-free;
 * #6 widens the vtable (present / glyph / dirty, render_ghostty) when SDL
 * is replaced by the native Screen backend. */
typedef struct renderer renderer_t;

typedef struct renderer_ops {
	int   (*init_symmenus)(renderer_t *r, void *screen, pref_t *prefs);
	void *(*symmenu_surface_for)(renderer_t *r, symmenu_t *menu);
	int   (*symmenu_height)(renderer_t *r, symmenu_t *menu);
	void  (*destroy)(renderer_t *r);
} renderer_ops_t;

/* Generic wrapper lifecycle (renderer.c). A backend builds its handle with
 * renderer_create(ops) + renderer_set_impl(); callers only use the
 * dispatchers below, so #6 swaps the backend without touching main.c. */
renderer_t *renderer_create(const renderer_ops_t *ops);
void  renderer_set_impl(renderer_t *r, void *impl);
void *renderer_impl(renderer_t *r);

int   renderer_init_symmenus(renderer_t *r, void *screen, pref_t *prefs);
void *renderer_symmenu_surface_for(renderer_t *r, symmenu_t *menu);
int   renderer_symmenu_height(renderer_t *r, symmenu_t *menu);
void  renderer_destroy(renderer_t *r);

/* SDL backend factory (renderer_sdl.c). #6 adds renderer_screen_create(). */
renderer_t *renderer_sdl_new(void);
const renderer_ops_t *renderer_sdl_ops(void);

#endif
