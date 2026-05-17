/*
 * Small project-owned value types shared across Term49 modules.
 *
 * Keep this header free of backend/vendor types such as SDL, libconfig,
 * BPS, or Lua. Module-specific structs belong in their own headers.
 */

#ifndef T49_TYPES_H_
#define T49_TYPES_H_

typedef struct t49_rgb {
	unsigned char r;
	unsigned char g;
	unsigned char b;
} t49_rgb_t;

typedef struct t49_rect {
	int x;
	int y;
	int w;
	int h;
} t49_rect_t;

/* Stable session handle id. 0 means "none" / "the active session" so that
 * actions and APIs default to the active session without churn when
 * multi-session (#4) lands. Lives here (not session.h) so action.h and
 * session.h can both use it without an include cycle. */
typedef unsigned int t49_session_id_t;

#endif /* T49_TYPES_H_ */
