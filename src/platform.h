#ifndef PLATFORM_H_
#define PLATFORM_H_

#include "event.h"

/* Backend-agnostic platform handle + operations. The platform owns the raw
 * event source (BPS in the native Screen backend) and OS services (virtual
 * keyboard, device query, notify, invoke). The run loop calls
 * platform_next_event; the backend folds key, touch, navigator, and VKB
 * events into a single source. */
typedef struct platform platform_t;

/* A persistent Hub notification (#35). The reuse key is (app_id, item_id):
 * posting again with the same pair updates that Hub entry in place rather than
 * stacking a new one. All fields are borrowed for the duration of the call.
 * target/action are intentionally absent -- Term49 only ever routes an invoke
 * back into itself, so the backend fixes them to its own id + bb.action.OPEN. */
typedef struct notification_spec {
	const char *app_id;   /* NULL => Term49's own identity (reliable reuse) */
	const char *item_id;  /* logical slot; NULL => a shared default. Same id replaces. */
	const char *title;    /* NULL => "Term49" */
	const char *body;     /* subtitle; may be NULL */
	const char *uri;      /* term49:// invoke payload; NULL => no invoke */
	int         alert;    /* 0 => notification_notify; 1 => notification_alert */
} notification_spec_t;

typedef struct platform_ops {
	int  (*next_event)(platform_t *p, event_t *out);   /* 1=event filled */
	void (*vkb_show)(platform_t *p);
	void (*vkb_hide)(platform_t *p);
	int  (*vkb_height)(platform_t *p);
	int  (*is_passport)(platform_t *p);
	/* Transient auto-dismissing flash (no Hub entry). 0 ok, -1 fail. */
	int  (*toast)(platform_t *p, const char *msg);
	/* Post/update a persistent, replaceable Hub entry (#35); when spec->uri is
	 * set, selecting it invokes Term49 back via the navigator (#23 round-trip).
	 * 0 ok, -1 fail. */
	int  (*notify)(platform_t *p, const notification_spec_t *spec);
	int  (*open_url)(platform_t *p, const char *url);  /* 0 ok, -1 fail */
	/* Set the event-pump idle timeout (ms): the longest next_event may block
	 * before returning with no event. Defaults to a power-friendly value; the
	 * touch arrow-pad gesture lowers it while armed so auto-repeat fires on a
	 * motionless finger, then restores it. May be NULL (no-op). */
	void (*set_idle_timeout)(platform_t *p, int ms);
	/* Apply any pending window-geometry changes (rotation + size + render
	 * buffer rebuild) stashed by next_event. Called from the main thread
	 * during the TERM_EVENT_RESIZE handler, before the renderer's next
	 * begin_frame latches a new buffer. Single-thread loop (#16-H), so
	 * there is no longer a separate render thread to race. No-op if
	 * nothing is pending. */
	void (*apply_pending_resize)(platform_t *p);
	/* Release backend-owned resources (window/context, BPS init, impl
	 * struct). Called from platform_destroy before the wrapper itself
	 * is freed. May be NULL for backends with no per-instance state. */
	void (*destroy)(platform_t *p);
} platform_ops_t;

/* Generic wrapper lifecycle (platform.c). */
platform_t *platform_create(const platform_ops_t *ops);
void  platform_set_impl(platform_t *p, void *impl);
void *platform_impl(platform_t *p);
void  platform_destroy(platform_t *p);

int  platform_next_event(platform_t *p, event_t *out);
void platform_vkb_show(platform_t *p);
void platform_vkb_hide(platform_t *p);
int  platform_vkb_height(platform_t *p);
int  platform_is_passport(platform_t *p);
int  platform_toast(platform_t *p, const char *msg);
int  platform_notify(platform_t *p, const notification_spec_t *spec);
int  platform_open_url(platform_t *p, const char *url);
/* Power-friendly default for the event-pump idle timeout; what the backend
 * starts at and what callers restore to after transiently lowering it. */
#define PLATFORM_IDLE_TIMEOUT_MS_DEFAULT 250
void platform_set_idle_timeout(platform_t *p, int ms);
void platform_apply_pending_resize(platform_t *p);

#endif
