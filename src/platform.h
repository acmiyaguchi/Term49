#ifndef PLATFORM_H_
#define PLATFORM_H_

#include "event.h"

/* Backend-agnostic platform handle + operations. The platform owns the raw
 * event source and OS services (virtual keyboard, device query, notify,
 * invoke). In #10 only the service forwarders are wired; next_event stays
 * NULL and the SDL pump remains inline in main(). #6 implements next_event
 * with the native Screen/BPS source and flips the run loop in one place. */
typedef struct t49_platform t49_platform_t;

typedef struct t49_platform_ops {
	int  (*next_event)(t49_platform_t *p, t49_event_t *out); /* 1=event; NULL in #10 */
	void (*vkb_show)(t49_platform_t *p);
	void (*vkb_hide)(t49_platform_t *p);
	int  (*vkb_height)(t49_platform_t *p);
	int  (*is_passport)(t49_platform_t *p);
	int  (*notify)(t49_platform_t *p, const char *msg);   /* -1 stub until #5 */
	int  (*open_url)(t49_platform_t *p, const char *url);  /* -1 stub until #5 */
} t49_platform_ops_t;

/* Generic wrapper lifecycle (platform.c). */
t49_platform_t *platform_create(const t49_platform_ops_t *ops);
void  platform_set_impl(t49_platform_t *p, void *impl);
void *platform_impl(t49_platform_t *p);
void  platform_destroy(t49_platform_t *p);

int  platform_next_event(t49_platform_t *p, t49_event_t *out);
void platform_vkb_show(t49_platform_t *p);
void platform_vkb_hide(t49_platform_t *p);
int  platform_vkb_height(t49_platform_t *p);
int  platform_is_passport(t49_platform_t *p);
int  platform_notify(t49_platform_t *p, const char *msg);
int  platform_open_url(t49_platform_t *p, const char *url);

/* SDL/BPS backend factory (platform_sdl.c). #6 adds platform_screen_create(). */
t49_platform_t *platform_sdl_create(void);
const t49_platform_ops_t *platform_sdl_ops(void);

#endif
