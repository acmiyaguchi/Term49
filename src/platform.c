/*
 * Backend-agnostic platform wrapper. Holds the vtable + the backend's
 * opaque impl pointer and forwards each operation. The concrete backend
 * (platform_sdl.c today; platform_screen.c in #6) only implements the ops
 * and a factory; no caller dereferences this struct.
 */

#include <stdlib.h>

#include "platform.h"

struct platform {
	const platform_ops_t *ops;
	void *impl;
};

platform_t *platform_create(const platform_ops_t *ops) {
	platform_t *p;
	if (ops == NULL) {
		return NULL;
	}
	p = calloc(1, sizeof(*p));
	if (p == NULL) {
		return NULL;
	}
	p->ops = ops;
	return p;
}

void platform_set_impl(platform_t *p, void *impl) {
	if (p != NULL) {
		p->impl = impl;
	}
}

void *platform_impl(platform_t *p) {
	return p != NULL ? p->impl : NULL;
}

void platform_destroy(platform_t *p) {
	free(p);
}

int platform_next_event(platform_t *p, event_t *out) {
	if (p == NULL || p->ops->next_event == NULL) {
		return 0;
	}
	return p->ops->next_event(p, out);
}

void platform_vkb_show(platform_t *p) {
	if (p != NULL && p->ops->vkb_show != NULL) {
		p->ops->vkb_show(p);
	}
}

void platform_vkb_hide(platform_t *p) {
	if (p != NULL && p->ops->vkb_hide != NULL) {
		p->ops->vkb_hide(p);
	}
}

int platform_vkb_height(platform_t *p) {
	if (p == NULL || p->ops->vkb_height == NULL) {
		return 0;
	}
	return p->ops->vkb_height(p);
}

int platform_is_passport(platform_t *p) {
	if (p == NULL || p->ops->is_passport == NULL) {
		return 0;
	}
	return p->ops->is_passport(p);
}

int platform_notify(platform_t *p, const char *msg) {
	if (p == NULL || p->ops->notify == NULL) {
		return -1;
	}
	return p->ops->notify(p, msg);
}

int platform_open_url(platform_t *p, const char *url) {
	if (p == NULL || p->ops->open_url == NULL) {
		return -1;
	}
	return p->ops->open_url(p, url);
}
