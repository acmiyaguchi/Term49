#ifndef RENDERER_SDL_H_
#define RENDERER_SDL_H_

#include "SDL.h"

#include "prefs.h"
#include "symmenu.h"

typedef struct renderer_sdl renderer_sdl_t;

renderer_sdl_t *renderer_sdl_create(void);
void renderer_sdl_destroy(renderer_sdl_t *renderer);

int renderer_sdl_init_symmenus(renderer_sdl_t *renderer, SDL_Surface *screen, pref_t *prefs);
SDL_Surface *renderer_sdl_symmenu_surface_for(renderer_sdl_t *renderer, symmenu_t *menu);
int renderer_sdl_symmenu_height(renderer_sdl_t *renderer, symmenu_t *menu);

#endif
