/*
 * Foot-style URL picker. Scans visible cells from the active session's
 * ghostty bridge for URLs, paints single-letter hint labels (two-letter
 * when there are >26 matches), then offers open / copy / QR actions for
 * the picked URL. Self-contained modal state; main.c calls url_pick_*
 * from the existing key-dispatch and render passes.
 */

#ifndef URL_PICK_H_
#define URL_PICK_H_

#include <stdint.h>

#include "ghostty_bridge.h"
#include "platform.h"
#include "renderer.h"

/* Pixel layout the picker needs to render. Computed by main.c (which
 * owns advance/text_height/fb_w/grid_top_pad) and passed in each call so
 * url_pick stays free of main.c internals. */
typedef struct url_pick_layout {
	int advance;          /* cell width in pixels */
	int text_height;      /* cell height in pixels (incl. padding) */
	int grid_top_pad;     /* pixels reserved for the tab strip */
	int fb_w;
	int fb_h;
	uint16_t cols;
	uint16_t rows;
} url_pick_layout_t;

/* Scan the active session's cell grid for URLs and arm hint mode.
 * Returns 1 on success (>=1 URL found, picker now active), 0 if no URLs
 * were found (state stays inactive, no UI change). */
int  url_pick_enter(ghostty_bridge_t *bridge, const url_pick_layout_t *layout);

/* True iff the picker is in any modal state (hint / action / qr).
 * Callers must swallow key + tap events while this is true. */
int  url_pick_active(void);

/* Dismiss the picker. Idempotent. */
void url_pick_exit(void);

/* Handle a key press while the picker is active. Returns 1 if the key
 * was consumed (always, while active). `sym` is a key_event_t::sym
 * (BB10 KEYCODE_*, ASCII-aligned for letters). Repeats are swallowed
 * without state change -- a held hint letter must not auto-fire its
 * action, and a held key during QR view must not re-enter the picker. */
int  url_pick_handle_key(int sym, int modifiers, int repeat, platform_t *platform);

/* Render hint labels / action prompt / QR over the framebuffer. No-op
 * when inactive. Call after the main cell draw pass. */
void url_pick_render(renderer_t *r, const url_pick_layout_t *layout);

#endif /* URL_PICK_H_ */
