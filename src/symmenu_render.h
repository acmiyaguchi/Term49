#ifndef SYMMENU_RENDER_H_
#define SYMMENU_RENDER_H_

#include "bitmap.h"
#include "prefs.h"
#include "symmenu.h"

typedef struct symmenu_render {
	symmenu_t *menu;
	bitmap_t  *surface;     /* RGBA8888, sized to the symmenu strip */
} symmenu_render_t;

/* Pre-composes the symmenu strip into an owned bitmap, exactly matching the
 * vendored SDL implementation (symmenu_sdl.c). screen_w/screen_h are the
 * current framebuffer dimensions; the strip sits at the bottom. As a side
 * effect each symkey's hitbox and uc fields are populated -- preserve this
 * behaviour, mousedown lookup relies on it. */
symmenu_render_t *symmenu_render_create(int screen_w, int screen_h,
                                        pref_t *prefs, symmenu_t *menu);
void symmenu_render_destroy(symmenu_render_t *render);

#endif /* SYMMENU_RENDER_H_ */
