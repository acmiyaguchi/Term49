#ifndef SYMMENU_SDL_H_
#define SYMMENU_SDL_H_

#include "SDL.h"
#include "prefs.h"
#include "symmenu.h"

typedef struct symmenu_sdl_render {
	symmenu_t *menu;
	SDL_Surface *surface;
} symmenu_sdl_render_t;

symmenu_sdl_render_t *symmenu_sdl_render(SDL_Surface *screen, pref_t *prefs, symmenu_t *menu);
void symmenu_sdl_destroy_render(symmenu_sdl_render_t *render);

#endif
