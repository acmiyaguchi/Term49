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

int app_init(app_t **out, const pref_t *prefs,
             uint16_t cols, uint16_t rows, size_t max_scrollback) {
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

	if (session_create(&s, 1, cols, rows, max_scrollback) != 0) {
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
	/* Must run before io_uninit() in app_shutdown(): the session owns its
	 * ghostty bridge (freed here) but still borrows the io master fd in
	 * this stage. Step 1.5 moves the fd into the session and tightens
	 * this ordering further. */
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

session_t *app_session_by_child_pid(app_t *app, pid_t pid) {
	if (app == NULL || pid <= 0) {
		return NULL;
	}
	for (unsigned i = 0; i < app->count; ++i) {
		if (session_child_pid(app->sessions[i]) == pid) {
			return app->sessions[i];
		}
	}
	return NULL;
}

unsigned app_session_count(const app_t *app) {
	return app ? app->count : 0;
}
