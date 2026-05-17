/*
 * Backend-agnostic app events.
 *
 * Platform implementations translate SDL_Event, BB10 screen_event_t, and BPS
 * events into this representation before app-level handling.
 */

#ifndef EVENT_H_
#define EVENT_H_

#include "term_types.h"

typedef enum event_type {
	TERM_EVENT_NONE,
	TERM_EVENT_QUIT,
	TERM_EVENT_KEY,
	TERM_EVENT_TOUCH_DOWN,
	TERM_EVENT_TOUCH_MOVE,
	TERM_EVENT_TOUCH_UP,
	TERM_EVENT_RESIZE,
	TERM_EVENT_ACTIVATE,
	TERM_EVENT_VKB,
} event_type_t;

typedef struct key_event {
	int keycode;
	int unicode;
	int modifiers;
	int pressed;
	int repeat;
	/* Rich screen-key fields (BB10 screen_event_t SCREEN_PROPERTY_KEY_*).
	 * sym mirrors keycode for the plain SDL path; flags carries the raw
	 * KEY_DOWN/KEY_REPEAT bits; alternate_sym is informational. */
	int sym;
	int alternate_sym;
	int flags;
} key_event_t;

typedef struct event {
	event_type_t type;
	union {
		key_event_t key;
		rect_t touch;
		struct { int w, h; } resize;
		struct { int active; int state; } activate;
		/* visible: 1 show, 0 hide, -1 height-only update (keep current
		 * visibility). height: reported keyboard height for the -1 case. */
		struct { int visible; int height; } vkb;
	} as;
} event_t;

#endif /* EVENT_H_ */
