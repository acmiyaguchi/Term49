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
	int  (*notify)(platform_t *p, const char *msg);    /* -1 stub until #5 */
	int  (*open_url)(platform_t *p, const char *url);  /* -1 stub until #5 */
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
int  platform_open_url(platform_t *p, const char *url);

#endif
