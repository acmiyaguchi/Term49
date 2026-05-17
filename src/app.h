/*
 * App-level state and dispatch boundary.
 *
 * This is the long-term convergence point for platform events, keybindings,
 * scripting/control actions, sessions, platform services, and rendering.
 */

#ifndef APP_H_
#define APP_H_

#include "action.h"
#include "event.h"
#include "prefs.h"

typedef struct t49_app t49_app_t;

int app_init(t49_app_t **out, const t49_prefs_t *prefs);
int app_handle_event(t49_app_t *app, const t49_event_t *event);
int app_dispatch_action(t49_app_t *app, const t49_action_t *action);
void app_shutdown_state(t49_app_t *app);

#endif /* APP_H_ */
