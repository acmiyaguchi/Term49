#ifndef SYMMENU_H_
#define SYMMENU_H_


#include "SDL.h"
#include "types.h"

typedef struct symmenu_render {
	symmenu_t *menu;
	SDL_Surface *surface;
} symmenu_render_t;

symmenu_render_t *render_symmenu(SDL_Surface *screen, pref_t *prefs, symmenu_t *menu);
void destroy_symmenu_render(symmenu_render_t *render);
void destroy_symmenu(symmenu_t *menu);

#endif
