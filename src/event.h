/*
 * Backend-agnostic app events.
 *
 * The platform backend translates raw BB10 screen_event_t and BPS events into
 * this representation before app-level handling.
 */

#ifndef EVENT_H_
#define EVENT_H_

#include <stddef.h>

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
	/* A cross-app navigator invocation targeting Term50 (#23): a bb.action.OPEN
	 * on a term:// URI. The backend copied the URI into backend-owned
	 * storage; the event's uri pointer borrows that storage and is valid only
	 * until the next platform_next_event, so the run loop must consume it in
	 * the same iteration. */
	TERM_EVENT_INVOKE,
} event_type_t;

/* What kind of invocation reached us. Only OPEN (a term:// URI via
 * bb.action.OPEN) is registered today. */
typedef enum invoke_action {
	TERM_INVOKE_OPEN,
} invoke_action_t;

typedef struct key_event {
	int keycode;
	int unicode;
	int modifiers;
	int pressed;
	int repeat;
	/* Rich screen-key fields (BB10 screen_event_t SCREEN_PROPERTY_KEY_*).
	 * sym mirrors keycode; alternate_sym is informational. The raw
	 * KEY_DOWN/KEY_REPEAT bits are decoded into pressed/repeat at the
	 * platform boundary and never cross this contract. */
	int sym;
	int alternate_sym;
} key_event_t;

typedef struct event {
	event_type_t type;
	union {
		key_event_t key;
		struct { int x, y; } touch;
		struct { int w, h; } resize;
		struct { int active; int state; } activate;
		/* visible: 1 show, 0 hide, -1 height-only update (keep current
		 * visibility). height: reported keyboard height for the -1 case. */
		struct { int visible; int height; } vkb;
		/* uri borrows backend-owned storage (see TERM_EVENT_INVOKE) and is
		 * NUL-terminated; e.g. "term://tab/2". */
		struct { invoke_action_t action; const char *uri; } invoke;
	} as;
} event_t;

#endif /* EVENT_H_ */
