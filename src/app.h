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

#include "action.h"
#include "event.h"
#include "prefs.h"
#include "session.h"

typedef struct app app_t;

int app_init(app_t **out, const pref_t *prefs,
             uint16_t cols, uint16_t rows, size_t max_scrollback);
int app_handle_event(app_t *app, const event_t *event);
int app_dispatch_action(app_t *app, const action_t *action);
void app_shutdown_state(app_t *app);

/* Session registry. Single session today; signatures are stable for #4. */
session_t *app_active_session(app_t *app);
session_t *app_session_by_id(app_t *app, session_id_t id); /* 0 => active */
unsigned       app_session_count(const app_t *app);

#endif /* APP_H_ */
