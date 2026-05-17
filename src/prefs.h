/*
 * Copyright (c) 2013 Todd Mortimer
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// Preferences contract. Keep file-format parser dependencies private to implementations.
#ifndef PREFS_H_
#define PREFS_H_


#include <unicode/utf.h>
#include "types.h"

#define PREFS_FILE_PATH ".term49rc"
#define T49_DEFAULT_FONT_PATH "/usr/fonts/font_repository/monotype/andalemo.ttf"
#define T49_DEFAULT_FONT_SIZE 24

typedef pref_t t49_prefs_t;

int preferences_guess_best_font_size(pref_t *prefs, int target_cols);

pref_t* read_preferences(const char* filename);
void save_preferences(pref_t const* pref, char const* filename);
void destroy_preferences(pref_t *pref);

const char* keystroke_lookup(char keystroke, keymap_t *keymap_head);
int is_int_member(int const* list, int target);

#endif /* PREFS_H_ */
