#ifndef RENDERER_SCREEN_H_
#define RENDERER_SCREEN_H_

#include "font.h"
#include "platform.h"
#include "renderer.h"

/* Native Screen renderer. Owns the framebuffer view, the symmenu cache, and
 * the glyph cache. The font is borrowed (lifecycle managed by main.c via
 * font_open/font_close); renderer_set_font swaps the borrowed pointer and
 * clears the cache. */
renderer_t *renderer_screen_create(platform_t *platform, font_t *font);

#endif /* RENDERER_SCREEN_H_ */
