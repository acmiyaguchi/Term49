#ifndef PLATFORM_H_
#define PLATFORM_H_

#include "event.h"

/* Backend-agnostic platform handle + operations. The platform owns the raw
 * event source and OS services (virtual keyboard, device query, notify,
 * invoke). In #10 the SDL pull pump is wired through next_event (the run loop
 * calls platform_next_event); the BB10 device key path and BPS VKB events
 * remain direct push callbacks. #6 replaces next_event with the native
 * Screen/BPS source and folds the push callbacks in, in one place. */
typedef struct platform platform_t;

typedef struct platform_ops {
	int  (*next_event)(platform_t *p, event_t *out); /* 1=event filled; SDL pull pump wired in #10 */
	void (*vkb_show)(platform_t *p);
	void (*vkb_hide)(platform_t *p);
	int  (*vkb_height)(platform_t *p);
	int  (*is_passport)(platform_t *p);
	int  (*notify)(platform_t *p, const char *msg);   /* -1 stub until #5 */
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

/* SDL/BPS backend factory (platform_sdl.c). #6 adds platform_screen_create(). */
platform_t *platform_sdl_create(void);
const platform_ops_t *platform_sdl_ops(void);

#endif
