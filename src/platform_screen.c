#include "platform_screen.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include <bps/bps.h>
#include <bps/deviceinfo.h>
#include <bps/dialog.h>
#include <bps/event.h>
#include <bps/navigator.h>
#include <bps/navigator_invoke.h>
#include <bps/notification.h>
#include <bps/screen.h>
#include <bps/virtualkeyboard.h>
#include <screen/screen.h>
#include <sys/keycodes.h>

#include "event.h"
#include "platform.h"

/* Cap on a single cross-app invoke URI (#23). A term49:// URI is short; without
 * a cap any app on the device could hand us an arbitrarily large copy.
 * Oversized URIs are truncated, not rejected. */
#define INVOKE_URI_MAX 4096

/* The invoke action we accept (the standard "open this" action; a tapped
 * notification, a homescreen shortcut, or a term49:// link in any app all
 * arrive as bb.action.OPEN), the URI scheme we register, and the target id
 * that addresses this app (must match bar-descriptor's <invoke-target id>).
 * Used both to receive (translate_navigator) and to post a notification that
 * invokes us back (screen_plat_notify). */
#define INVOKE_ACTION_OPEN "bb.action.OPEN"
#define INVOKE_URI_SCHEME  "term49://"
#define INVOKE_TARGET_ID   "com.example.Term49"

/* Defaults for a notification posted without a caller-supplied slot/title (#35):
 * a single shared item_id so bare posts coalesce, and the app name as title. */
#define NOTIFY_DEFAULT_ITEM_ID "term49"
#define NOTIFY_DEFAULT_TITLE   "Term49"

typedef struct platform_screen {
	screen_context_t ctx;
	screen_window_t  window;
	int              orientation_angle;
	int              last_w;
	int              last_h;
	/* Pending orientation/geometry change stashed by translate_navigator
	 * and applied by screen_plat_apply_pending_resize on the main thread
	 * during the TERM_EVENT_RESIZE handler. Stashed (not applied
	 * directly) so the destructive buffer rebuild happens at a known
	 * point in the loop, not inside the platform event translator. */
	int              pending_resize_valid;
	int              pending_resize_angle;
	int              pending_resize_w;
	int              pending_resize_h;
	/* Latest invoke URI (#23). translate_navigator copies the navigator
	 * invocation's URI here; the emitted TERM_EVENT_INVOKE borrows it. Valid
	 * only until the next next_event overwrites it, which is safe because the
	 * run loop consumes the event synchronously in the same iteration.
	 * NUL-terminated. */
	char             invoke_uri[INVOKE_URI_MAX];
} platform_screen_t;

static platform_screen_t *self_of(platform_t *p) {
	return (platform_screen_t *)platform_impl(p);
}

static const char *window_group_name(char *buf, size_t buflen) {
	const char *env = getenv("WINDOW_GROUP_ID");
	if (env != NULL && *env != '\0') {
		return env;
	}
	snprintf(buf, buflen, "Term49-%d", (int)getpid());
	return buf;
}

static int read_display_size(int *w, int *h) {
	const char *we = getenv("WIDTH");
	const char *he = getenv("HEIGHT");
	if (we != NULL && he != NULL) {
		*w = atoi(we);
		*h = atoi(he);
		return 1;
	}
	return 0;
}

static int translate_screen(platform_screen_t *self, bps_event_t *event, event_t *out) {
	(void)self;
	screen_event_t se = screen_event_get_event(event);
	if (se == NULL) {
		return 0;
	}
	int type = 0;
	screen_get_event_property_iv(se, SCREEN_PROPERTY_TYPE, &type);
	switch (type) {
	case SCREEN_EVENT_KEYBOARD: {
		int screen_val = 0, screen_flags = 0, screen_alt_val = 0, modifiers = 0;
		screen_get_event_property_iv(se, SCREEN_PROPERTY_KEY_FLAGS, &screen_flags);
		screen_get_event_property_iv(se, SCREEN_PROPERTY_KEY_SYM, &screen_val);
		screen_get_event_property_iv(se, SCREEN_PROPERTY_KEY_ALTERNATE_SYM, &screen_alt_val);
		screen_get_event_property_iv(se, SCREEN_PROPERTY_KEY_MODIFIERS, &modifiers);
		memset(out, 0, sizeof(*out));
		out->type = TERM_EVENT_KEY;
		out->as.key.sym           = screen_val;
		out->as.key.keycode       = screen_val;
		out->as.key.unicode       = screen_val;
		out->as.key.alternate_sym = screen_alt_val;
		out->as.key.modifiers     = modifiers;
		out->as.key.pressed       = (screen_flags & KEY_DOWN)   ? 1 : 0;
		out->as.key.repeat        = (screen_flags & KEY_REPEAT) ? 1 : 0;
		return 1;
	}
	case SCREEN_EVENT_MTOUCH_TOUCH:
	case SCREEN_EVENT_MTOUCH_MOVE:
	case SCREEN_EVENT_MTOUCH_RELEASE: {
		int pos[2] = {0, 0};
		if (screen_get_event_property_iv(se, SCREEN_PROPERTY_SOURCE_POSITION, pos) != 0) {
			/* Drop touches whose position we couldn't read — otherwise the
			 * zero-init pos would emit a synthetic (0,0) touch and trip the
			 * metamode hitbox in the top-left corner. */
			return 0;
		}
		if (pos[1] < 0) {
			/* Negative-Y events are bezel swipes; the vendored SDL
			 * also dropped these. */
			return 0;
		}
		memset(out, 0, sizeof(*out));
		switch (type) {
		case SCREEN_EVENT_MTOUCH_TOUCH:   out->type = TERM_EVENT_TOUCH_DOWN; break;
		case SCREEN_EVENT_MTOUCH_MOVE:    out->type = TERM_EVENT_TOUCH_MOVE; break;
		case SCREEN_EVENT_MTOUCH_RELEASE: out->type = TERM_EVENT_TOUCH_UP;   break;
		}
		out->as.touch.x = pos[0];
		out->as.touch.y = pos[1];
		return 1;
	}
	default:
		return 0;
	}
}

static int translate_navigator(platform_screen_t *self, bps_event_t *event, event_t *out) {
	int code = bps_event_get_code(event);
	switch (code) {
	case NAVIGATOR_ORIENTATION_CHECK:
		navigator_orientation_check_response(event, getenv("AUTO_ORIENTATION") != NULL);
		return 0;
	case NAVIGATOR_ORIENTATION: {
		int angle = navigator_event_get_orientation_angle(event);
		int diff  = abs(angle - self->orientation_angle);
		int new_w = self->last_w;
		int new_h = self->last_h;
		if (diff == 90 || diff == 270) {
			new_w = self->last_h;
			new_h = self->last_w;
		}
		/* Stash the new geometry but don't touch the window or the
		 * render buffers here: the resize is destructive (it tears
		 * down + recreates the render buffers, invalidating the
		 * renderer's per-buffer freshness table) and we want it to
		 * happen at the well-defined TERM_EVENT_RESIZE handler point
		 * in the main loop, not partway through this event translator. */
		self->pending_resize_valid = 1;
		self->pending_resize_angle = angle;
		self->pending_resize_w     = new_w;
		self->pending_resize_h     = new_h;
		memset(out, 0, sizeof(*out));
		out->type = TERM_EVENT_RESIZE;
		out->as.resize.w = new_w;
		out->as.resize.h = new_h;
		navigator_done_orientation(event);
		return 1;
	}
	case NAVIGATOR_WINDOW_STATE: {
		navigator_window_state_t state = navigator_event_get_window_state(event);
		memset(out, 0, sizeof(*out));
		out->type = TERM_EVENT_ACTIVATE;
		out->as.activate.active = (state == NAVIGATOR_WINDOW_FULLSCREEN) ? 1 : 0;
		out->as.activate.state  = 0;
		return 1;
	}
	case NAVIGATOR_EXIT:
		memset(out, 0, sizeof(*out));
		out->type = TERM_EVENT_QUIT;
		return 1;
	case NAVIGATOR_INVOKE_TARGET: {
		/* A cross-app invocation reached us (#23). The OS has already
		 * re-foregrounded Term49; we just extract the term49:// URI and
		 * fold a TERM_EVENT_INVOKE for the run loop to act on. One-way:
		 * no reply is sent (the protocol is fire-and-forget like OSC). */
		const navigator_invoke_invocation_t *inv =
			navigator_invoke_event_get_invocation(event);
		if (inv == NULL) {
			return 0;
		}
		const char *action = navigator_invoke_invocation_get_action(inv);
		const char *uri    = navigator_invoke_invocation_get_uri(inv);
		/* The bar-descriptor filter should only route OPEN on our scheme
		 * here, but verify both defensively before acting. */
		if (action == NULL || strcmp(action, INVOKE_ACTION_OPEN) != 0 ||
		    uri == NULL || strncmp(uri, INVOKE_URI_SCHEME,
		                           sizeof(INVOKE_URI_SCHEME) - 1) != 0) {
			return 0;
		}
		size_t n = strlen(uri);
		if (n > INVOKE_URI_MAX - 1) {
			n = INVOKE_URI_MAX - 1;  /* truncate; never overrun */
		}
		memcpy(self->invoke_uri, uri, n);
		self->invoke_uri[n] = '\0';
		memset(out, 0, sizeof(*out));
		out->type = TERM_EVENT_INVOKE;
		out->as.invoke.action = TERM_INVOKE_OPEN;
		out->as.invoke.uri    = self->invoke_uri;
		return 1;
	}
	default:
		return 0;
	}
}

static int translate_vkb(platform_screen_t *self, bps_event_t *event, event_t *out) {
	(void)self;
	int code = bps_event_get_code(event);
	memset(out, 0, sizeof(*out));
	out->type = TERM_EVENT_VKB;
	switch (code) {
	case VIRTUALKEYBOARD_EVENT_VISIBLE:
		out->as.vkb.visible = 1;
		out->as.vkb.height  = 0;
		return 1;
	case VIRTUALKEYBOARD_EVENT_HIDDEN:
		out->as.vkb.visible = 0;
		out->as.vkb.height  = 0;
		return 1;
	case VIRTUALKEYBOARD_EVENT_INFO:
		out->as.vkb.visible = -1;
		out->as.vkb.height  = (int)virtualkeyboard_event_get_height(event);
		return 1;
	default:
		out->type = TERM_EVENT_NONE;
		return 0;
	}
}

static int screen_plat_next_event(platform_t *p, event_t *out) {
	platform_screen_t *self = self_of(p);
	if (self == NULL || out == NULL) {
		return 0;
	}
	bps_event_t *event = NULL;
	/* 250 ms timeout instead of indefinite block: belt-and-braces for
	 * the bps_add_fd path. The pty / SIGCHLD io_handlers push a no-op
	 * wake event after dirtying state, which should return the pump
	 * immediately — but if that mechanism ever drops a wake, the timeout
	 * still pulls the loop back so the render check can fire. Idle cost
	 * is 4 wakes/sec; the main loop fast-paths through them when nothing
	 * is dirty. */
	if (bps_get_event(&event, 250) != BPS_SUCCESS || event == NULL) {
		memset(out, 0, sizeof(*out));
		out->type = TERM_EVENT_NONE;
		return 0;
	}
	int domain = bps_event_get_domain(event);
	if (domain == screen_get_domain())          return translate_screen(self, event, out);
	if (domain == navigator_get_domain())       return translate_navigator(self, event, out);
	if (domain == virtualkeyboard_get_domain()) return translate_vkb(self, event, out);
	memset(out, 0, sizeof(*out));
	out->type = TERM_EVENT_NONE;
	return 0;
}

static void screen_plat_vkb_show(platform_t *p) { (void)p; virtualkeyboard_show(); }
static void screen_plat_vkb_hide(platform_t *p) { (void)p; virtualkeyboard_hide(); }
static int  screen_plat_vkb_height(platform_t *p) {
	(void)p;
	int h = 0;
	if (virtualkeyboard_get_height(&h) != BPS_SUCCESS) {
		return 0;
	}
	return h;
}

static int screen_plat_is_passport(platform_t *p) {
	(void)p;
	deviceinfo_details_t *di = NULL;
	if (deviceinfo_get_details(&di) != BPS_SUCCESS) {
		return 0;
	}
	int passport = 0;
	const char *model = deviceinfo_details_get_model_name(di);
	if (model != NULL && strncmp("Passport", model, 8) == 0) {
		passport = 1;
	}
	deviceinfo_free_details(&di);
	return passport;
}

/* Transient toast via the navigator. The toast auto-dismisses, but the
 * dialog object lives until destroyed; we keep a single handle and tear
 * down the previous toast before showing the next so at most one leaks
 * (reclaimed at process exit). No dialog events are requested -- a toast
 * has no buttons, so there is nothing to handle. */
static int screen_plat_toast(platform_t *p, const char *msg) {
	(void)p;
	static dialog_instance_t toast = NULL;
	if (msg == NULL || msg[0] == '\0') {
		return -1;
	}
	if (toast != NULL) {
		dialog_destroy(toast);
		toast = NULL;
	}
	if (dialog_create_toast(&toast) != BPS_SUCCESS) {
		toast = NULL;
		return -1;
	}
	if (dialog_set_toast_message_text(toast, msg) != BPS_SUCCESS ||
	    dialog_show(toast) != BPS_SUCCESS) {
		dialog_destroy(toast);
		toast = NULL;
		return -1;
	}
	return 0;
}

/* Copy an id (item_id / app_id) into buf, keeping only [A-Za-z0-9_] -- the
 * documented charset for these fields. Disallowed bytes (e.g. a hyphen) become
 * '_' so a caller's logical name still maps to one stable, reusable slot rather
 * than being rejected. Empty/NULL input yields the supplied fallback. */
static const char *sanitize_id(const char *in, const char *fallback,
                               char *buf, size_t cap) {
	if (in == NULL || in[0] == '\0') {
		return fallback;
	}
	size_t j = 0;
	for (size_t i = 0; in[i] != '\0' && j + 1 < cap; i++) {
		unsigned char ch = (unsigned char)in[i];
		int ok = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
		         (ch >= '0' && ch <= '9') || ch == '_';
		buf[j++] = ok ? (char)ch : '_';
	}
	buf[j] = '\0';
	return j > 0 ? buf : fallback;
}

/* Post (or update) a persistent Hub entry (#35). The (app_id, item_id) pair is
 * the reuse key: posting again with the same pair updates that entry in place
 * instead of stacking a new one, so callers reuse a slot by reusing item_id.
 * app_id defaults to our own identity (the only one reuse is reliable for);
 * item_id defaults to a single shared slot so bare posts coalesce. target/action
 * are fixed -- the only invoke Term49 routes is back into itself -- and a
 * term49:// payload uri (optional) drives the #23 round-trip when tapped. The
 * message owns no resources past the send, so it's destroyed immediately. Needs
 * the post_notification permission (already in bar-descriptor.xml).
 *
 * Note: in-place update only holds within a single launch. Re-posting an item_id
 * that survived from a previous run (the app exited without clearing it) fails
 * rather than updating the stale Hub entry -- the notification manager no longer
 * recognizes the slot as ours. Reuse is therefore reliable only for ids minted
 * during the current process lifetime. */
static int screen_plat_notify(platform_t *p, const notification_spec_t *spec) {
	(void)p;
	notification_message_t *m = NULL;
	char app_buf[64];
	char item_buf[64];

	if (spec == NULL || notification_message_create(&m) != BPS_SUCCESS || m == NULL) {
		return -1;
	}

	const char *app_id  = sanitize_id(spec->app_id, INVOKE_TARGET_ID,
	                                  app_buf, sizeof(app_buf));
	const char *item_id = sanitize_id(spec->item_id, NOTIFY_DEFAULT_ITEM_ID,
	                                  item_buf, sizeof(item_buf));
	const char *title   = (spec->title != NULL && spec->title[0] != '\0')
	                          ? spec->title : NOTIFY_DEFAULT_TITLE;

	int ok =
		notification_message_set_app_id(m, app_id) == BPS_SUCCESS &&
		notification_message_set_item_id(m, item_id) == BPS_SUCCESS &&
		notification_message_set_title(m, title) == BPS_SUCCESS &&
		(spec->body == NULL ||
		 notification_message_set_subtitle(m, spec->body) == BPS_SUCCESS) &&
		notification_message_set_invocation_target(m, INVOKE_TARGET_ID) == BPS_SUCCESS &&
		notification_message_set_invocation_action(m, INVOKE_ACTION_OPEN) == BPS_SUCCESS &&
		(spec->uri == NULL ||
		 notification_message_set_invocation_payload_uri(m, spec->uri) == BPS_SUCCESS) &&
		(spec->alert ? notification_alert(m) : notification_notify(m)) == BPS_SUCCESS;

	notification_message_destroy(&m);
	return ok ? 0 : -1;
}

/* Hand the URI to the platform default handler (browser, file viewer,
 * etc.) via the legacy one-shot navigator_invoke. */
static int screen_plat_open_url(platform_t *p, const char *url) {
	(void)p;
	if (url == NULL || url[0] == '\0') {
		return -1;
	}
	return navigator_invoke(url, NULL) == BPS_SUCCESS ? 0 : -1;
}

static void screen_plat_apply_pending_resize(platform_t *p) {
	platform_screen_t *self = self_of(p);
	if (self == NULL || !self->pending_resize_valid) {
		return;
	}
	int angle = self->pending_resize_angle;
	int size[2] = {self->pending_resize_w, self->pending_resize_h};
	screen_set_window_property_iv(self->window, SCREEN_PROPERTY_ROTATION, &angle);
	screen_set_window_property_iv(self->window, SCREEN_PROPERTY_SIZE, size);
	screen_set_window_property_iv(self->window, SCREEN_PROPERTY_SOURCE_SIZE, size);
	/* Buffers were sized to the old geometry. Tear them down and recreate at
	 * the new size so the next latch_framebuffer views the correct dimensions
	 * and screen_post_window doesn't stretch / clip. */
	screen_destroy_window_buffers(self->window);
	if (screen_create_window_buffers(self->window, 2) != 0) {
		fprintf(stderr, "platform_screen: screen_create_window_buffers failed during resize\n");
	}
	self->orientation_angle  = angle;
	self->last_w             = self->pending_resize_w;
	self->last_h             = self->pending_resize_h;
	self->pending_resize_valid = 0;
}

static void screen_plat_destroy(platform_t *p) {
	platform_screen_t *self = self_of(p);
	if (self == NULL) {
		return;
	}
	/* Mirrors the fail-path teardown in platform_screen_create: drop
	 * the window (which also unwinds the window buffers), the screen
	 * context, the impl struct, and finally bps_shutdown to release
	 * the screen / navigator / vkb event registrations. */
	screen_destroy_window(self->window);
	screen_destroy_context(self->ctx);
	free(self);
	platform_set_impl(p, NULL);
	bps_shutdown();
}

static const platform_ops_t SCREEN_PLATFORM_OPS = {
	.next_event           = screen_plat_next_event,
	.vkb_show             = screen_plat_vkb_show,
	.vkb_hide             = screen_plat_vkb_hide,
	.vkb_height           = screen_plat_vkb_height,
	.is_passport          = screen_plat_is_passport,
	.toast                = screen_plat_toast,
	.notify               = screen_plat_notify,
	.open_url             = screen_plat_open_url,
	.apply_pending_resize = screen_plat_apply_pending_resize,
	.destroy              = screen_plat_destroy,
};

const platform_ops_t *platform_screen_ops(void) {
	return &SCREEN_PLATFORM_OPS;
}

platform_t *platform_screen_create(void) {
	if (bps_initialize() != BPS_SUCCESS) {
		fprintf(stderr, "platform_screen: bps_initialize failed\n");
		return NULL;
	}
	platform_screen_t *self = calloc(1, sizeof(*self));
	if (self == NULL) {
		goto fail_bps;
	}

	if (screen_create_context(&self->ctx, SCREEN_APPLICATION_CONTEXT) != 0) {
		fprintf(stderr, "platform_screen: screen_create_context failed\n");
		goto fail_self;
	}
	if (screen_create_window(&self->window, self->ctx) != 0) {
		fprintf(stderr, "platform_screen: screen_create_window failed\n");
		goto fail_ctx;
	}

	char buf[64];
	const char *group = window_group_name(buf, sizeof(buf));
	if (screen_create_window_group(self->window, group) != 0) {
		fprintf(stderr, "platform_screen: screen_create_window_group failed\n");
		goto fail_window;
	}

	int format = SCREEN_FORMAT_RGBA8888;
	int usage  = SCREEN_USAGE_NATIVE | SCREEN_USAGE_ROTATION;
	/* SCREEN_SENSITIVITY_TEST (the default) is what we want: the window
	 * receives keyboard/pointer/touch when visible. SCREEN_SENSITIVITY_NEVER
	 * silently swallows every input event — the app shows but cannot be
	 * typed into. */
	int sens   = SCREEN_SENSITIVITY_TEST;
	screen_set_window_property_iv(self->window, SCREEN_PROPERTY_FORMAT, &format);
	screen_set_window_property_iv(self->window, SCREEN_PROPERTY_USAGE,  &usage);
	screen_set_window_property_iv(self->window, SCREEN_PROPERTY_SENSITIVITY, &sens);

	int w = 0, h = 0;
	if (!read_display_size(&w, &h)) {
		int size[2] = {0, 0};
		if (screen_get_window_property_iv(self->window, SCREEN_PROPERTY_SIZE, size) == 0) {
			w = size[0];
			h = size[1];
		}
	}
	if (w <= 0 || h <= 0) {
		fprintf(stderr, "platform_screen: could not determine display size\n");
		goto fail_window;
	}
	int size[2] = {w, h};
	screen_set_window_property_iv(self->window, SCREEN_PROPERTY_SIZE, size);
	screen_set_window_property_iv(self->window, SCREEN_PROPERTY_SOURCE_SIZE, size);

	const char *orient_env = getenv("ORIENTATION");
	int angle = (orient_env != NULL) ? atoi(orient_env) : 0;
	screen_set_window_property_iv(self->window, SCREEN_PROPERTY_ROTATION, &angle);
	self->orientation_angle = angle;
	self->last_w = w;
	self->last_h = h;

	/* IDLE_MODE: SCREEN_IDLE_NORMAL env present => standard timeout;
	 * absent => keep the screen awake. main() sets the env from
	 * prefs->screen_idle_awake before calling platform_screen_create. */
	int idle_mode = (getenv("SCREEN_IDLE_NORMAL") != NULL)
	              ? SCREEN_IDLE_MODE_NORMAL
	              : SCREEN_IDLE_MODE_KEEP_AWAKE;
	screen_set_window_property_iv(self->window, SCREEN_PROPERTY_IDLE_MODE, &idle_mode);

	if (screen_create_window_buffers(self->window, 2) != 0) {
		fprintf(stderr, "platform_screen: screen_create_window_buffers failed\n");
		goto fail_window;
	}

	if (screen_request_events(self->ctx) != BPS_SUCCESS ||
	    navigator_request_events(0)      != BPS_SUCCESS ||
	    virtualkeyboard_request_events(0)!= BPS_SUCCESS) {
		fprintf(stderr, "platform_screen: event registration failed\n");
		goto fail_window;
	}

	platform_t *p = platform_create(platform_screen_ops());
	if (p == NULL) {
		goto fail_window;
	}
	platform_set_impl(p, self);
	return p;

fail_window:
	screen_destroy_window(self->window);
fail_ctx:
	screen_destroy_context(self->ctx);
fail_self:
	free(self);
fail_bps:
	bps_shutdown();
	return NULL;
}

screen_context_t platform_screen_context(platform_t *p) {
	platform_screen_t *self = self_of(p);
	return self != NULL ? self->ctx : NULL;
}

screen_window_t platform_screen_window(platform_t *p) {
	platform_screen_t *self = self_of(p);
	return self != NULL ? self->window : NULL;
}
