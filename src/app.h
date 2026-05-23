/*
 * App-level state and dispatch boundary.
 *
 * This is the long-term convergence point for platform events, keybindings,
 * scripting/control actions, sessions, platform services, and rendering.
 */

#ifndef APP_H_
#define APP_H_

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include "action.h"
#include "event.h"
#include "prefs.h"
#include "session.h"

/* Hard cap on simultaneous sessions. A phone keyboard makes a small fixed
 * vector friendlier than a heap-grown one (no realloc invalidating fd
 * pointers across SIGCHLD), and 8 tabs is plenty for the form factor. */
#define APP_MAX_SESSIONS 8

typedef struct app app_t;

int app_init(app_t **out, const pref_t *prefs,
             uint16_t cols, uint16_t rows, size_t max_scrollback);
int app_handle_event(app_t *app, const event_t *event);
int app_dispatch_action(app_t *app, const action_t *action);
void app_shutdown_state(app_t *app);

/* Session registry. */
session_t *app_active_session(app_t *app);
session_t *app_session_at(app_t *app, unsigned index);
session_t *app_session_by_id(app_t *app, session_id_t id);     /* 0 => active */
session_t *app_session_by_child_pid(app_t *app, pid_t pid);    /* NULL if none */
int        app_session_index_of(app_t *app, const session_t *s, unsigned *out);
unsigned   app_session_count(const app_t *app);
unsigned   app_active_index(const app_t *app);                 /* 0..count-1 */

/* TAB_* dispatch helpers. Each marks the screen dirty; main.c handles
 * the pty spawn / close ioctls so app.c stays free of unix specifics. */
int  app_session_open(app_t *app, uint16_t cols, uint16_t rows,
                      size_t max_scrollback, session_t **out);
void app_session_close_index(app_t *app, unsigned index);  /* frees + reflows */
void app_session_select_next(app_t *app);
void app_session_select_prev(app_t *app);
/* Make the tab at visible index `index` (0..count-1) active. Returns 1 if the
 * index was in range and selected, 0 otherwise (active left unchanged). */
int  app_session_select_index(app_t *app, unsigned index);

#endif /* APP_H_ */
