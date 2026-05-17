#include <stdlib.h>

#include "symmenu.h"

void destroy_symmenu(symmenu_t *menu) {
	for (symkey_t **row = menu->keys; row != NULL; ++row) {
		for (symkey_t *key = *row; key->map != NULL; ++key) {
			free(key->uc);
			free(key);
		}
		free(row);
	}
	
	free(menu->entries);
}
