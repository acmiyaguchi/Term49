#include <stdio.h>
#include <string.h>

#include <bps/bps.h>

#include "SDL.h"
#include "SDL_syswm.h"

#include "platform_sdl.h"

int platform_sdl_translate_event(const SDL_Event *event, t49_event_t *out) {
	if (event == NULL || out == NULL) {
		return 0;
	}

	memset(out, 0, sizeof(*out));
	out->type = T49_EVENT_NONE;

	switch (event->type) {
	case SDL_QUIT:
		out->type = T49_EVENT_QUIT;
		return 1;
	case SDL_VIDEORESIZE:
		out->type = T49_EVENT_RESIZE;
		out->as.resize.w = event->resize.w;
		out->as.resize.h = event->resize.h;
		return 1;
	case SDL_KEYDOWN:
		out->type = T49_EVENT_KEY;
		out->as.key.keycode = event->key.keysym.sym;
		out->as.key.unicode = event->key.keysym.sym;
		out->as.key.pressed = 1;
		return 1;
	case SDL_MOUSEBUTTONDOWN:
		out->type = T49_EVENT_TOUCH_DOWN;
		out->as.touch.x = event->button.x;
		out->as.touch.y = event->button.y;
		return 1;
	case SDL_ACTIVEEVENT:
		out->type = T49_EVENT_ACTIVATE;
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
