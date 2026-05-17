/*
 * Backend-agnostic app events.
 *
 * Platform implementations translate SDL_Event, BB10 screen_event_t, and BPS
 * events into this representation before app-level handling.
 */

#ifndef EVENT_H_
#define EVENT_H_

#include "t49_types.h"

typedef enum t49_event_type {
	T49_EVENT_NONE,
	T49_EVENT_QUIT,
	T49_EVENT_KEY,
	T49_EVENT_TOUCH_DOWN,
	T49_EVENT_TOUCH_MOVE,
	T49_EVENT_TOUCH_UP,
	T49_EVENT_RESIZE,
	T49_EVENT_ACTIVATE,
	T49_EVENT_VKB,
} t49_event_type_t;

typedef struct t49_key_event {
	int keycode;
	int unicode;
	int modifiers;
	int pressed;
	int repeat;
} t49_key_event_t;

typedef struct t49_event {
	t49_event_type_t type;
	union {
		t49_key_event_t key;
		t49_rect_t touch;
		struct { int w, h; } resize;
		struct { int active; int state; } activate;
		struct { int visible; int height; } vkb;
	} as;
} t49_event_t;

#endif /* EVENT_H_ */
