#ifndef PLATFORM_SDL_H_
#define PLATFORM_SDL_H_

#include "SDL.h"

#include "event.h"

/* Translate one SDL event into the backend-agnostic app event model.
 * Returns 1 when out contains an app event, 0 when the raw event is ignored. */
int platform_sdl_translate_event(const SDL_Event *event, event_t *out);

#endif
