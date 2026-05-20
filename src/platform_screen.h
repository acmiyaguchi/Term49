/*
 * Native Screen + BPS platform backend (#6). Implements platform_ops_t
 * directly against the BB10 NDK without going through the vendored SDL.
 *
 * Folds the two push callbacks that libSDL12.so used to invoke directly
 * (handleKeyboardEvent on screen-key events, handle_virtualkeyboard_event
 * on BPS VKB events) into next_event so the abstraction is no longer
 * punctured by hidden ABI calls from a shared library.
 */

#ifndef PLATFORM_SCREEN_H_
#define PLATFORM_SCREEN_H_

#include <screen/screen.h>

#include "platform.h"

platform_t           *platform_screen_create(void);
const platform_ops_t *platform_screen_ops(void);

/* Native handles for the renderer backend (#6). renderer_screen_create()
 * pulls these through its platform_t argument; main.c never sees them. */
screen_context_t platform_screen_context(platform_t *p);
screen_window_t  platform_screen_window(platform_t *p);

#endif /* PLATFORM_SCREEN_H_ */
