/*
 * Small project-owned value types shared across Term49 modules.
 *
 * Keep this header free of backend/vendor types such as SDL, libconfig,
 * BPS, or Lua. Module-specific structs belong in their own headers.
 */

#ifndef TERM_TYPES_H_
#define TERM_TYPES_H_

typedef struct rgb {
	unsigned char r;
	unsigned char g;
	unsigned char b;
} rgb_t;

typedef struct rect {
	int x;
	int y;
	int w;
	int h;
} rect_t;

/* Stable session handle id. 0 means "none" / "the active session" so that
 * actions and APIs default to the active session without churn when
 * multi-session (#4) lands. Lives here (not session.h) so action.h and
 * session.h can both use it without an include cycle. */
typedef unsigned int session_id_t;

#endif /* TERM_TYPES_H_ */
