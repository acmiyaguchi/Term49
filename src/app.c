/*
 * Real app: lifecycle + session registry. The app owns exactly one
 * session in this stage. app_handle_event() / app_dispatch_action() remain
 * defined in main.c (they touch window/render globals); this file only owns
 * app construction/teardown and session lookup.
 *
 * App/window UI state (metamode, vmodifiers, current_symmenu, ...) stays
 * file-static in main.c for now: it is read by the render thread under
 * input_mutex, and relocating it would change that locking. #4 adopts it
 * here once there is genuinely more than one session.
 */

#include <stdlib.h>

#include "app.h"

struct app {
	const pref_t *prefs;   /* borrowed; freed by the caller */
	session_t *sessions[1];
	unsigned count;
	session_id_t active;    /* id of the active session */
};

int app_init(app_t **out, const pref_t *prefs) {
	app_t *app;
	session_t *s = NULL;

	if (out == NULL) {
		return -1;
	}

	app = calloc(1, sizeof(*app));
	if (app == NULL) {
		return -1;
	}

	app->prefs = prefs;

	if (session_create(&s, 1) != 0) {
		free(app);
		return -1;
	}

	app->sessions[0] = s;
	app->count = 1;
	app->active = 1;

	*out = app;
	return 0;
}

void app_shutdown_state(app_t *app) {
	if (app == NULL) {
		return;
	}
	/* Must run before ghostty_bridge_uninit()/io_uninit() in app_shutdown(),
	 * since the single session borrows both. */
	session_destroy(app->sessions[0]);
	free(app);
}

session_t *app_active_session(app_t *app) {
	if (app == NULL) {
		return NULL;
	}
	return app->sessions[0];
}

session_t *app_session_by_id(app_t *app, session_id_t id) {
	if (app == NULL) {
		return NULL;
	}
	if (id == 0 || id == app->active) {   /* 0 => active session */
		return app->sessions[0];
	}
	return NULL;
}

unsigned app_session_count(const app_t *app) {
	return app ? app->count : 0;
}
