#include <stdio.h>
#include <string.h>

#include <bps/bps.h>
#include <bps/virtualkeyboard.h>

#include "SDL.h"
#include "SDL_syswm.h"

#include "platform.h"
#include "platform_sdl.h"
#include "terminal.h"

int platform_sdl_translate_event(const SDL_Event *event, event_t *out) {
	if (event == NULL || out == NULL) {
		return 0;
	}

	memset(out, 0, sizeof(*out));
	out->type = TERM_EVENT_NONE;

	switch (event->type) {
	case SDL_QUIT:
		out->type = TERM_EVENT_QUIT;
		return 1;
	case SDL_VIDEORESIZE:
		out->type = TERM_EVENT_RESIZE;
		out->as.resize.w = event->resize.w;
		out->as.resize.h = event->resize.h;
		return 1;
	case SDL_KEYDOWN:
		/* The device screen-key path does not arrive here (the prebuilt
		 * SDL calls handleKeyboardEvent directly); this covers any plain
		 * SDL keydown (e.g. an external keyboard). Build a rich key event
		 * so app_handle_key() treats it like a non-repeat key press.
		 * out is memset to 0 above, so modifiers/repeat stay 0. */
		out->type = TERM_EVENT_KEY;
		out->as.key.sym = event->key.keysym.sym;
		out->as.key.keycode = event->key.keysym.sym;
		out->as.key.unicode = event->key.keysym.sym;
		out->as.key.pressed = 1;
		return 1;
	case SDL_MOUSEBUTTONDOWN:
		out->type = TERM_EVENT_TOUCH_DOWN;
		out->as.touch.x = event->button.x;
		out->as.touch.y = event->button.y;
		return 1;
	case SDL_ACTIVEEVENT:
		out->type = TERM_EVENT_ACTIVATE;
		out->as.activate.active = event->active.gain;
		out->as.activate.state = event->active.state;
		return 1;
	case SDL_SYSWMEVENT:
		if (event->syswm.msg != NULL && event->syswm.msg->event != NULL) {
			bps_event_t *bps_event = event->syswm.msg->event;
			fprintf(stderr, "Unhandled SYSWMEVENT: %d\n", bps_event_get_domain(bps_event));
		}
		return 0;
	default:
		fprintf(stderr, "Unknown Event: %d\n", event->type);
		return 0;
	}
}

/* --- platform vtable adapter (the seam #6 swaps) ---
 * next_event drives the SDL pull pump: SDL_WaitEvent + translate behind the
 * vtable, called from main()'s run loop via platform_next_event(). It owns
 * only the SDL pull source; the BB10 device key path (handleKeyboardEvent)
 * and BPS VKB events stay direct push callbacks until #6 replaces this with
 * the native Screen/BPS source. SDL_WaitEvent blocks, so the run loop calls
 * this outside lock_input() (the wait must not hold input_mutex). */

static int sdl_plat_next_event(platform_t *p, event_t *out) {
	SDL_Event raw;
	(void)p;
	/* SDL_WaitEvent returns 0 on error; do not translate an uninitialized
	 * event. Returning 0 keeps the run loop's unconditional render poke. */
	if (SDL_WaitEvent(&raw) == 0) {
		return 0;
	}
	return platform_sdl_translate_event(&raw, out);
}

static void sdl_plat_vkb_show(platform_t *p)  { (void)p; virtualkeyboard_show(); }
static void sdl_plat_vkb_hide(platform_t *p)  { (void)p; virtualkeyboard_hide(); }
static int  sdl_plat_vkb_height(platform_t *p){ (void)p; return get_virtualkeyboard_height(); }
static int  sdl_plat_is_passport(platform_t *p){ (void)p; return is_passport(); }
static int  sdl_plat_notify(platform_t *p, const char *msg)  { (void)p; (void)msg; return -1; }
static int  sdl_plat_open_url(platform_t *p, const char *url){ (void)p; (void)url; return -1; }

static const platform_ops_t SDL_PLATFORM_OPS = {
	.next_event  = sdl_plat_next_event,
	.vkb_show    = sdl_plat_vkb_show,
	.vkb_hide    = sdl_plat_vkb_hide,
	.vkb_height  = sdl_plat_vkb_height,
	.is_passport = sdl_plat_is_passport,
	.notify      = sdl_plat_notify,
	.open_url    = sdl_plat_open_url,
};

const platform_ops_t *platform_sdl_ops(void) {
	return &SDL_PLATFORM_OPS;
}

platform_t *platform_sdl_create(void) {
	return platform_create(platform_sdl_ops());
}
