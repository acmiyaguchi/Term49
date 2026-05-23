#ifndef PLATFORM_H_
#define PLATFORM_H_

#include "event.h"

/* Backend-agnostic platform handle + operations. The platform owns the raw
 * event source (BPS in the native Screen backend) and OS services (virtual
 * keyboard, device query, notify, invoke). The run loop calls
 * platform_next_event; the backend folds key, touch, navigator, and VKB
 * events into a single source. */
typedef struct platform platform_t;

typedef struct platform_ops {
	int  (*next_event)(platform_t *p, event_t *out);   /* 1=event filled */
	void (*vkb_show)(platform_t *p);
	void (*vkb_hide)(platform_t *p);
	int  (*vkb_height)(platform_t *p);
	int  (*is_passport)(platform_t *p);
	int  (*notify)(platform_t *p, const char *msg);    /* 0 ok, -1 fail */
	/* Post a persistent, tappable notification-center entry whose selection
	 * invokes Term49 back via the navigator (#23 round-trip). payload is the
	 * RUN action's text/plain body (e.g. "tab=2"). 0 ok, -1 fail. */
	int  (*notify_invoke)(platform_t *p, const char *msg, const char *payload);
	int  (*open_url)(platform_t *p, const char *url);  /* 0 ok, -1 fail */
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
int  platform_notify(platform_t *p, const char *msg);
int  platform_notify_invoke(platform_t *p, const char *msg, const char *payload);
int  platform_open_url(platform_t *p, const char *url);
void platform_apply_pending_resize(platform_t *p);

#endif
