/*
 * App: lifecycle + session registry. Owns the vector of sessions and the
 * active-tab index. main.c handles pty spawn/teardown (it's the unix
 * boundary), and dispatches TAB_* actions through the helpers here.
 *
 * App/window UI state (metamode, vmodifiers, current_symmenu, ...) stays
 * file-static in main.c: the single-threaded event loop reads/writes it
 * from event handlers and the render path on the same thread, so moving
 * it here would just split UI logic across files for no gain.
 */

#include <stdlib.h>

#include "app.h"

struct app {
	const pref_t *prefs;   /* borrowed; freed by the caller */
	session_t *sessions[APP_MAX_SESSIONS];
	unsigned count;
	unsigned active;       /* index into sessions[]; valid iff count > 0 */
	session_id_t next_id;  /* monotonically increases; never reused */
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
	app->next_id = 1;

	if (session_create(&s, app->next_id++, cols, rows, max_scrollback) != 0) {
		free(app);
		return -1;
	}

	app->sessions[0] = s;
	app->count = 1;
	app->active = 0;

	*out = app;
	return 0;
}

void app_shutdown_state(app_t *app) {
	if (app == NULL) {
		return;
	}
	for (unsigned i = 0; i < app->count; ++i) {
		session_destroy(app->sessions[i]);
		app->sessions[i] = NULL;
	}
	free(app);
}

session_t *app_active_session(app_t *app) {
	if (app == NULL || app->count == 0) {
		return NULL;
	}
	return app->sessions[app->active];
}

session_t *app_session_at(app_t *app, unsigned index) {
	if (app == NULL || index >= app->count) {
		return NULL;
	}
	return app->sessions[index];
}

session_t *app_session_by_id(app_t *app, session_id_t id) {
	if (app == NULL || app->count == 0) {
		return NULL;
	}
	if (id == 0) {
		return app->sessions[app->active];
	}
	for (unsigned i = 0; i < app->count; ++i) {
		if (session_id(app->sessions[i]) == id) {
			return app->sessions[i];
		}
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

int app_session_index_of(app_t *app, const session_t *s, unsigned *out) {
	if (app == NULL || s == NULL) {
		return 0;
	}
	for (unsigned i = 0; i < app->count; ++i) {
		if (app->sessions[i] == s) {
			if (out != NULL) { *out = i; }
			return 1;
		}
	}
	return 0;
}

unsigned app_session_count(const app_t *app) {
	return app ? app->count : 0;
}

unsigned app_active_index(const app_t *app) {
	return app ? app->active : 0;
}

int app_session_open(app_t *app, uint16_t cols, uint16_t rows,
                     size_t max_scrollback, session_t **out) {
	session_t *s = NULL;

	if (app == NULL || app->count >= APP_MAX_SESSIONS) {
		return -1;
	}

	if (session_create(&s, app->next_id, cols, rows, max_scrollback) != 0) {
		return -1;
	}

	app->next_id++;
	app->sessions[app->count] = s;
	app->active = app->count;
	app->count++;

	if (out != NULL) {
		*out = s;
	}
	return 0;
}

void app_session_close_index(app_t *app, unsigned index) {
	if (app == NULL || index >= app->count) {
		return;
	}

	session_destroy(app->sessions[index]);

	/* Compact the vector left of the closed slot to keep the active index
	 * meaningful and to avoid holes in the FD_SET fan-out. */
	for (unsigned i = index; i + 1 < app->count; ++i) {
		app->sessions[i] = app->sessions[i + 1];
	}
	app->count--;
	app->sessions[app->count] = NULL;

	if (app->count == 0) {
		app->active = 0;
		return;
	}

	if (app->active >= app->count) {
		app->active = app->count - 1;
	}
}

void app_session_select_next(app_t *app) {
	if (app == NULL || app->count == 0) {
		return;
	}
	app->active = (app->active + 1) % app->count;
}

void app_session_select_prev(app_t *app) {
	if (app == NULL || app->count == 0) {
		return;
	}
	app->active = (app->active + app->count - 1) % app->count;
}

int app_session_select_index(app_t *app, unsigned index) {
	if (app == NULL || index >= app->count) {
		return 0;
	}
	app->active = index;
	return 1;
}
