#ifndef SYMMENU_H_
#define SYMMENU_H_


#include "SDL.h"
#include "types.h"

SDL_Surface *render_symmenu(SDL_Surface *screen, pref_t *prefs, symmenu_t *menu);
void destroy_symmenu(symmenu_t *menu);

#endif
