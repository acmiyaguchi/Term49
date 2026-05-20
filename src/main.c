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

#include <ioctl.h>
#include <unix.h>
#include <termios.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <time.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/keycodes.h>
#include <unistd.h>

#include <bps/screen.h>
#include <bps/virtualkeyboard.h>
#include <bps/deviceinfo.h>
#include <unicode/utf.h>

#include <pthread.h>
#include <stdint.h>

#include "bitmap.h"
#include "font.h"
#include "types.h"
#include "terminal.h"
#include "action.h"
#include "app.h"
#include "prefs.h"
#include "symmenu.h"
#include "renderer.h"
#include "renderer_screen.h"
#include "platform.h"
#include "platform_screen.h"
#include "io.h"
#include "ghostty_bridge.h"

static int exit_application = 0;

/* The launcher hands us HOME = /accounts/<id>/appdata/<appid>/data,
 * which is wiped on every app reinstall and is invisible to the File
 * Manager / bb-scp. Repoint HOME at the persistent, cross-app shared
 * Documents dir (the OS creates it once access_shared is granted) so
 * dotfiles, mksh history and .term49rc survive redeploys and can be
 * seeded from the dev box. Also export TERMINFO with an absolute path
 * to the bundled terminfo, since the shared filesystem is FUSE-backed
 * and rejects the ~/.terminfo symlink we used to rely on. Falls back
 * to the sandbox HOME untouched if anything is unexpected. */
static void set_persistent_home(void) {
	const char* sandbox_home = getenv("HOME");
	const char* p;
	const char* appid_end;
	char buf[1024];

	if (sandbox_home == NULL) { return; }

	p = strstr(sandbox_home, "/appdata/");
	if (p == NULL) { return; }
	appid_end = strchr(p + strlen("/appdata/"), '/');
	if (appid_end == NULL) { return; }

	/* TERMINFO = <...>/appdata/<appid>/app/native/terminfo */
	{
		int n = snprintf(buf, sizeof(buf), "%.*s/app/native/terminfo",
		                 (int)(appid_end - sandbox_home), sandbox_home);
		if (n > 0 && n < (int)sizeof(buf)) {
			setenv("TERMINFO", buf, 1);
		}
	}

	/* HOME = <...>/shared/documents (sibling of /appdata) */
	{
		int n = snprintf(buf, sizeof(buf), "%.*s/shared/documents",
		                 (int)(p - sandbox_home), sandbox_home);
		if (n <= 0 || n >= (int)sizeof(buf)) {
			return;
		}
	}
	/* Only repoint HOME if the target really is a usable directory:
	 * created just now, or already existing AND a readable / writable /
	 * searchable directory. Accepting a bare EEXIST would repoint HOME at
	 * an existing-but-unusable path, and the later chdir(home) would then
	 * silently fail, leaving the shell in the sandbox dir with a HOME it
	 * cannot use. Verify before committing; otherwise keep sandbox HOME. */
	{
		struct stat st;
		if ((mkdir(buf, 0700) == 0 ||
		     (errno == EEXIST && stat(buf, &st) == 0 && S_ISDIR(st.st_mode)))
		    && access(buf, R_OK | W_OK | X_OK) == 0) {
			setenv("HOME", buf, 1);
		} else {
			fprintf(stderr,
			        "Could not use persistent HOME %s: %s - keeping sandbox HOME\n",
			        buf, strerror(errno));
		}
	}
}

static char slave_ptyname[L_ctermid];

static char draw_cursor = 1;

static char flash = 0;

static pref_t *prefs = NULL;
static symmenu_t *current_symmenu = NULL;
static renderer_t *renderer = NULL;
static app_t *g_app = NULL;
static platform_t *g_platform = NULL;
/* Set by the reload_config builtin; consumed at a safe point in the main
 * loop (never inside action dispatch / a lua_pcall). Same input thread,
 * same lock as event handling, so a plain int is sufficient. */
static int g_reload_pending = 0;

static char symmenu_lock = 0;
static char altsym_lock = 0;

static char metamode = 0;
static int metamode_doubletap_key = 0;
static struct timespec metamode_last;
static rgb_t metamode_cursor_fg = TERM_COLOR_BLACK;
static rgb_t metamode_cursor_bg = TERM_COLOR_GREEN;
static bitmap_t *metamode_cursor;
static int vmodifiers = 0;

static font_t *font;
static int text_width;
static int text_height;
static int text_height_padding;
static int advance;
static int fb_w;
static int fb_h;

/* Frame-level dirty gate. A repaint is needed only when this is set.
 * Always written under input_mutex (every writer below already holds
 * lock_input()), so no atomics needed. Start dirty for the first frame. */
static int screen_dirty = 1;
static int screen_full_dirty = 1;

static void mark_screen_dirty(int full_repaint) {
	screen_dirty = 1;
	if (full_repaint) {
		screen_full_dirty = 1;
	}
}

static bitmap_t *ctrl_key_indicator;
static bitmap_t *alt_key_indicator;
static bitmap_t *shift_key_indicator;
static bitmap_t *altsym_indicator;

static pid_t child_pid = -1;

static char virtualkeyboard_visible = 0;
static char key_repeat_done = 0;

static pthread_mutex_t input_mutex;

static int event_pipe[2];

static int rows;
static int cols;

/* Init flags guard app_shutdown against the early-exit paths in main() that
 * fire before startup_init() has set up the corresponding resources.
 * Destroying an uninitialized mutex, closing an unopened pipe, or
 * FT_Done_FreeType on a never-initialized library is undefined behaviour. */
static int input_mutex_inited;
static int event_pipe_open;
static int font_library_inited;
static int font_inited;
static int ghostty_inited;


int is_terminfo_keystrokes(const char* keystrokes){
	if(keystrokes[0] == 'k'){
		if(0 == strncmp(keystrokes, "kcub1", 5)){ return KEYCODE_LEFT; }
		if(0 == strncmp(keystrokes, "kcud1", 5)){ return KEYCODE_DOWN; }
		if(0 == strncmp(keystrokes, "kcuf1", 5)){ return KEYCODE_RIGHT; }
		if(0 == strncmp(keystrokes, "kcuu1", 5)){ return KEYCODE_UP; }
		if(0 == strncmp(keystrokes, "khome", 5)){ return KEYCODE_HOME; }
		if(0 == strncmp(keystrokes, "kend", 4)){ return KEYCODE_END; }
		if(0 == strncmp(keystrokes, "kf1", 3)){ return KEYCODE_F1; }
		if(0 == strncmp(keystrokes, "kf2", 3)){ return KEYCODE_F2; }
		if(0 == strncmp(keystrokes, "kf3", 3)){ return KEYCODE_F3; }
		if(0 == strncmp(keystrokes, "kf4", 3)){ return KEYCODE_F4; }
		if(0 == strncmp(keystrokes, "kf5", 3)){ return KEYCODE_F5; }
		if(0 == strncmp(keystrokes, "kf6", 3)){ return KEYCODE_F6; }
		if(0 == strncmp(keystrokes, "kf7", 3)){ return KEYCODE_F7; }
		if(0 == strncmp(keystrokes, "kf8", 3)){ return KEYCODE_F8; }
		if(0 == strncmp(keystrokes, "kf9", 3)){ return KEYCODE_F9; }
		if(0 == strncmp(keystrokes, "kf10", 4)){ return KEYCODE_F10; }
		if(0 == strncmp(keystrokes, "kf11", 4)){ return KEYCODE_F11; }
		if(0 == strncmp(keystrokes, "kf12", 4)){ return KEYCODE_F12; }
	}
	return 0;
}

static int terminal_csi_key(UChar *tbuf, char code) {
	tbuf[0] = 033;
	tbuf[1] = '[';
	tbuf[2] = code;
	return 3;
}

static int terminal_esc_o_key(UChar *tbuf, char code) {
	tbuf[0] = 033;
	tbuf[1] = 'O';
	tbuf[2] = code;
	return 3;
}

static int terminal_func_key(UChar *tbuf, int num) {
	int nc = 3;
	tbuf[0] = 033;
	tbuf[1] = '[';
	switch(num){
		case 1: tbuf[2] = '1'; nc = 4; break;
		case 2: tbuf[2] = '2'; nc = 4; break;
		case 3: tbuf[2] = '3'; nc = 4; break;
		case 4: tbuf[2] = '4'; nc = 4; break;
		case 5: tbuf[2] = '5'; nc = 4; break;
		case 6: tbuf[2] = '6'; nc = 4; break;
		case 15: tbuf[2] = '1'; tbuf[3] = '5'; nc = 5; break;
		case 17: tbuf[2] = '1'; tbuf[3] = '7'; nc = 5; break;
		case 18: tbuf[2] = '1'; tbuf[3] = '8'; nc = 5; break;
		case 19: tbuf[2] = '1'; tbuf[3] = '9'; nc = 5; break;
		case 20: tbuf[2] = '2'; tbuf[3] = '0'; nc = 5; break;
		case 21: tbuf[2] = '2'; tbuf[3] = '1'; nc = 5; break;
		case 23: tbuf[2] = '2'; tbuf[3] = '3'; nc = 5; break;
		case 24: tbuf[2] = '2'; tbuf[3] = '4'; nc = 5; break;
	}
	tbuf[nc - 1] = '~';
	return nc;
}

static int terminal_key_sequence(int sym, int mod, UChar *tbuf) {
	int num_chars = 1;
	tbuf[0] = (UChar)sym;

	switch (sym){
		case KEYCODE_BACKSPACE: tbuf[0] = 010; break;
		case KEYCODE_TAB:       tbuf[0] = 011; break;
		case KEYCODE_ESCAPE:    tbuf[0] = 033; break;
		case KEYCODE_UP:        return terminal_csi_key(tbuf, 'A');
		case KEYCODE_DOWN:      return terminal_csi_key(tbuf, 'B');
		case KEYCODE_RIGHT:     return terminal_csi_key(tbuf, 'C');
		case KEYCODE_LEFT:      return terminal_csi_key(tbuf, 'D');
		case KEYCODE_RETURN:    tbuf[0] = 015; break;
		case KEYCODE_KP_ENTER:  tbuf[0] = 015; break;
		case KEYCODE_DELETE:    return terminal_func_key(tbuf, 3);
		case KEYCODE_INSERT:    return terminal_func_key(tbuf, 2);
		case KEYCODE_HOME:      return terminal_csi_key(tbuf, 'H');
		case KEYCODE_END:       return terminal_csi_key(tbuf, 'F');
		case KEYCODE_PG_UP:     return terminal_func_key(tbuf, 5);
		case KEYCODE_PG_DOWN:   return terminal_func_key(tbuf, 6);
		case KEYCODE_BACK_TAB:  return terminal_csi_key(tbuf, 'Z');
		case KEYCODE_F1:        return terminal_esc_o_key(tbuf, 'P');
		case KEYCODE_F2:        return terminal_esc_o_key(tbuf, 'Q');
		case KEYCODE_F3:        return terminal_esc_o_key(tbuf, 'R');
		case KEYCODE_F4:        return terminal_esc_o_key(tbuf, 'S');
		case KEYCODE_F5:        return terminal_func_key(tbuf, 15);
		case KEYCODE_F6:        return terminal_func_key(tbuf, 17);
		case KEYCODE_F7:        return terminal_func_key(tbuf, 18);
		case KEYCODE_F8:        return terminal_func_key(tbuf, 19);
		case KEYCODE_F9:        return terminal_func_key(tbuf, 20);
		case KEYCODE_F10:       return terminal_func_key(tbuf, 21);
		case KEYCODE_F11:       return terminal_func_key(tbuf, 23);
		case KEYCODE_F12:       return terminal_func_key(tbuf, 24);
	}

	if((mod & KEYMOD_SHIFT) || (mod & KEYMOD_SHIFT_LOCK) || (mod & KEYMOD_CAPS_LOCK)){
		switch(sym){
			case KEYCODE_A: tbuf[0] = 'A'; break;
			case KEYCODE_B: tbuf[0] = 'B'; break;
			case KEYCODE_C: tbuf[0] = 'C'; break;
			case KEYCODE_D: tbuf[0] = 'D'; break;
			case KEYCODE_E: tbuf[0] = 'E'; break;
			case KEYCODE_F: tbuf[0] = 'F'; break;
			case KEYCODE_G: tbuf[0] = 'G'; break;
			case KEYCODE_H: tbuf[0] = 'H'; break;
			case KEYCODE_I: tbuf[0] = 'I'; break;
			case KEYCODE_J: tbuf[0] = 'J'; break;
			case KEYCODE_K: tbuf[0] = 'K'; break;
			case KEYCODE_L: tbuf[0] = 'L'; break;
			case KEYCODE_M: tbuf[0] = 'M'; break;
			case KEYCODE_N: tbuf[0] = 'N'; break;
			case KEYCODE_O: tbuf[0] = 'O'; break;
			case KEYCODE_P: tbuf[0] = 'P'; break;
			case KEYCODE_Q: tbuf[0] = 'Q'; break;
			case KEYCODE_R: tbuf[0] = 'R'; break;
			case KEYCODE_S: tbuf[0] = 'S'; break;
			case KEYCODE_T: tbuf[0] = 'T'; break;
			case KEYCODE_U: tbuf[0] = 'U'; break;
			case KEYCODE_V: tbuf[0] = 'V'; break;
			case KEYCODE_W: tbuf[0] = 'W'; break;
			case KEYCODE_X: tbuf[0] = 'X'; break;
			case KEYCODE_Y: tbuf[0] = 'Y'; break;
			case KEYCODE_Z: tbuf[0] = 'Z'; break;
		}
	}

	if(mod & KEYMOD_CTRL){
		switch (sym) {
			case KEYCODE_SPACE: tbuf[0] = 000; break;
			case KEYCODE_A: tbuf[0] = 001; break;
			case KEYCODE_B: tbuf[0] = 002; break;
			case KEYCODE_C: tbuf[0] = 003; break;
			case KEYCODE_D: tbuf[0] = 004; break;
			case KEYCODE_E: tbuf[0] = 005; break;
			case KEYCODE_F: tbuf[0] = 006; break;
			case KEYCODE_G: tbuf[0] = 007; break;
			case KEYCODE_H: tbuf[0] = 010; break;
			case KEYCODE_I: tbuf[0] = 011; break;
			case KEYCODE_J: tbuf[0] = 012; break;
			case KEYCODE_K: tbuf[0] = 013; break;
			case KEYCODE_L: tbuf[0] = 014; break;
			case KEYCODE_M: tbuf[0] = 015; break;
			case KEYCODE_N: tbuf[0] = 016; break;
			case KEYCODE_O: tbuf[0] = 017; break;
			case KEYCODE_P: tbuf[0] = 020; break;
			case KEYCODE_Q: tbuf[0] = 021; break;
			case KEYCODE_R: tbuf[0] = 022; break;
			case KEYCODE_S: tbuf[0] = 023; break;
			case KEYCODE_T: tbuf[0] = 024; break;
			case KEYCODE_U: tbuf[0] = 025; break;
			case KEYCODE_V: tbuf[0] = 026; break;
			case KEYCODE_W: tbuf[0] = 027; break;
			case KEYCODE_X: tbuf[0] = 030; break;
			case KEYCODE_Y: tbuf[0] = 031; break;
			case KEYCODE_Z: tbuf[0] = 032; break;
			case KEYCODE_LEFT_BRACKET: tbuf[0] = 033; break;
			case KEYCODE_BACK_SLASH: tbuf[0] = 034; break;
			case KEYCODE_RIGHT_BRACKET: tbuf[0] = 035; break;
			case KEYCODE_GRAVE: if(mod & KEYMOD_SHIFT){tbuf[0] = 036;} break;
			case KEYCODE_SLASH: if(mod & KEYMOD_SHIFT){tbuf[0] = 037;} break;
		}
	}
	return num_chars;
}


int send_metamode_keystrokes(const char* keystrokes){

	UChar* ukeystrokes;
	size_t ukeystrokes_len;
	size_t keystrokes_len;
	int terminfo_key = 0;
	UChar terminfo_keystrokes[CHARACTER_BUFFER];

	if(keystrokes){
		terminfo_key = is_terminfo_keystrokes(keystrokes);
		/* if the keystrokes for this key match a terminfo pattern,
		 * send the appropriate sequence instead of the literal string */
		if(terminfo_key){
			ukeystrokes_len = terminal_key_sequence(terminfo_key, 0, terminfo_keystrokes);
			/* and write out to the tty whatever the keys were */
			io_write_master(terminfo_keystrokes, ukeystrokes_len);
			return 1;
		}
		// else
		keystrokes_len = strlen(keystrokes);
		/* config strings are ascii, but we can put utf8 in there too */
		ukeystrokes = (UChar*)calloc(keystrokes_len, sizeof(UChar));
		ukeystrokes_len = io_read_utf8_string(keystrokes, keystrokes_len, ukeystrokes);
		/* and write out to the tty whatever the keys were */
		io_write_master(ukeystrokes, ukeystrokes_len);
		free(ukeystrokes);
		return 1;
	}
	/* no keystrokes saved for this key */
	return 0;
}

/* These local-UI mutators are the funnel for keyboard/touch-driven
 * screen changes that never round-trip the pty (metamode cursor,
 * symmenu overlay, modifier indicators, font reflow). Marking dirty
 * here, at the mutation, is what makes the render gate correct
 * regardless of which event source delivered the input. */
void metamode_toggle(){
	metamode = metamode ? 0 : 1;
	mark_screen_dirty(1);
}

void altsym_toggle() {
	altsym_lock = altsym_lock ? 0 : 1;
	mark_screen_dirty(1);
}

void symmenu_stick(){
	PRINT(stderr, "Sticking Sym key\n");
	symmenu_lock = 1;
	mark_screen_dirty(1);
}

void symmenu_toggle(symmenu_t *target){
	if (current_symmenu == NULL){
		int symmenu_height = renderer_symmenu_height(renderer, target);
		if (target == NULL || symmenu_height <= 0) {
			return;
		}
		current_symmenu = target;
		// resize to show menu
		if (prefs->rescreen_for_symmenu) {
			setup_screen_size(fb_w, fb_h - symmenu_height);
		}
		if (prefs->sticky_sym_key) {
			symmenu_stick();
		}
	} else {
		current_symmenu = NULL;
		if (prefs->rescreen_for_symmenu) {
			// resize to take full screen
			setup_screen_size(fb_w, fb_h);
		}
		symmenu_lock = 0;
	}
	mark_screen_dirty(1);
}

static keymap_t* symkey_for_mousedown(symmenu_t *menu, uint16_t x, uint16_t y) {
	for (int row = 0; menu->keys[row] != NULL; ++row) {
		for (int col = 0; menu->keys[row][col].map != NULL; ++col) {
			symkey_t *key = &menu->keys[row][col];

			if((x >= key->hitbox.x) &&
			   (x <= key->hitbox.x + key->hitbox.w) &&
			   (y >= key->hitbox.y) &&
			   (y <= key->hitbox.y + key->hitbox.h)) {
				if (!symmenu_lock) {
					symmenu_toggle(NULL);
				} else {
					key->flash = 1;
				}
				return key->map;
			}
		}
	}
	
	return NULL;
}

int font_init(int font_size){
	if(font_size < MIN_FONT_SIZE){
		fprintf(stderr, "Refusing to set font size to %d - too small\n",font_size);
		int default_font_columns = (atoi(getenv("WIDTH")) <= 720) ? 45 : 60;
		font_size = preferences_guess_best_font_size(prefs, default_font_columns);
	}

	/* Load the font */
	font = font_open(prefs->font_path, font_size);
	if ( font == NULL ) {
		fprintf(stderr, "Couldn't load %d pt font from %s\n", font_size, prefs->font_path);
		font = font_open(TERM_DEFAULT_FONT_PATH, TERM_DEFAULT_FONT_SIZE);
		if(font == NULL){
			fprintf(stderr, "Could not open default font %s\n", TERM_DEFAULT_FONT_PATH);
			return TERM_FAILURE;
		}
	}
	PRINT(stderr, "Font is Fixed Width: %d\n", font_is_fixed_width(font));

	/* initialize modifier indicator glyphs */
	alt_key_indicator = font_render_glyph_shaded(font, 'A', FONT_STYLE_NORMAL,
	                                             metamode_cursor_fg, metamode_cursor_bg);
	if (alt_key_indicator == NULL){
		PRINT(stderr, "Couldn't render alt_key_indicator\n");
		return TERM_FAILURE;
	}

	ctrl_key_indicator = font_render_glyph_shaded(font, 'C', FONT_STYLE_NORMAL,
	                                              metamode_cursor_fg, metamode_cursor_bg);
	if (ctrl_key_indicator == NULL){
		PRINT(stderr, "Couldn't render ctrl_key_indicator\n");
		return TERM_FAILURE;
	}

	shift_key_indicator = font_render_glyph_shaded(font, 0x2191, FONT_STYLE_NORMAL,
	                                               metamode_cursor_fg, metamode_cursor_bg);
	if (shift_key_indicator == NULL){
		PRINT(stderr, "Couldn't render shift_key_indicator\n");
		return TERM_FAILURE;
	}

	altsym_indicator = font_render_glyph_shaded(font, 'a', FONT_STYLE_NORMAL,
	                                            metamode_cursor_fg, metamode_cursor_bg);
	if (altsym_indicator == NULL){
		PRINT(stderr, "Couldn't render altsym_indicator\n");
		return TERM_FAILURE;
	}

	metamode_cursor = font_render_glyph_shaded(font, 'M', FONT_STYLE_NORMAL,
	                                           metamode_cursor_fg, metamode_cursor_bg);
	if (metamode_cursor == NULL){
		PRINT(stderr, "Couldn't render metamode_cursor\n");
		return TERM_FAILURE;
	}

	/* Get the size of the font */
	int minx, maxx, miny, maxy;
	if(font_glyph_metrics(font, 'X', &minx, &maxx, &miny, &maxy, &advance) != 0){
		PRINT(stderr, "Could not get Glyph Metrics for 'X'\n");
		return TERM_FAILURE;
	}

	text_width = advance;
	text_height = maxy - miny;
	text_height_padding = font_line_skip(font) - text_height;
	text_height += text_height_padding;
	PRINT(stderr, "Character h: %d w:%d (h padding: %d) advance: %d\n", text_height, text_width, text_height_padding, advance);

	if (renderer != NULL) {
		renderer_set_font(renderer, font);
	}
	return TERM_SUCCESS;
}

void font_uninit(){

	if (renderer != NULL) {
		renderer_set_font(renderer, NULL);
	}
	bitmap_free(metamode_cursor);     metamode_cursor     = NULL;
	bitmap_free(ctrl_key_indicator);  ctrl_key_indicator  = NULL;
	bitmap_free(alt_key_indicator);   alt_key_indicator   = NULL;
	bitmap_free(shift_key_indicator); shift_key_indicator = NULL;
	bitmap_free(altsym_indicator);    altsym_indicator    = NULL;
	if(font != NULL){
		font_close(font);
	}
}

void handle_activeevent(int gain, int state){
	if (gain && prefs->auto_show_vkb){
		PRINT(stderr, "Got ActiveEvent - initializing keyboard\n");
		platform_vkb_show(g_platform);
	}
}

static void maybe_show_vkb(void) {
	/* On a Passport the system gesture for the VKB doesn't work, so we
	 * reveal it on a screen tap. Triggered from TOUCH_UP rather than
	 * TOUCH_DOWN so a drag-to-scroll gesture doesn't pop the keyboard. */
	if (prefs->auto_show_vkb) {
		platform_vkb_show(g_platform);
	}
}

void handle_mousedown(uint16_t x, uint16_t y){
	/* check for hits in the metamode_hitbox */
	if((x >= prefs->metamode_hitbox->x) &&
	   (x <= prefs->metamode_hitbox->x + prefs->metamode_hitbox->w) &&
	   (y >= prefs->metamode_hitbox->y) &&
	   (y <= prefs->metamode_hitbox->y + prefs->metamode_hitbox->h)) {
		/* hit in the box */
		metamode_toggle();
	}

	/* check for symmenu touches */
	if(current_symmenu != NULL){
		keymap_t *entry = symkey_for_mousedown(current_symmenu, x, y);
		if (entry != NULL) {
			app_dispatch_action(g_app, &entry->action);
		}
	}
}

/* Single-finger drag-scrollback state. libghostty owns the viewport
 * offset; we only convert pixel drag into a row delta and call
 * ghostty_bridge_scroll_view. `locked` is latched at TOUCH_DOWN so a
 * symmenu tap whose press dismisses the menu can't then scroll if the
 * finger keeps moving before release. */
static struct {
	int     active;
	int     committed;
	int     locked;
	int16_t start_y;
	int16_t last_y;
	int     accum_dy;
} g_drag;

static void drag_reset(void) {
	g_drag.active    = 0;
	g_drag.committed = 0;
	g_drag.locked    = 0;
	g_drag.start_y   = 0;
	g_drag.last_y    = 0;
	g_drag.accum_dy  = 0;
}

static int render_ghostty(int force_full_repaint); /* defined below */

void rescreen(int w, int h){

	int width  = w == -1 ? fb_w : w;
	int height = h == -1 ? fb_h : h;
	int vkb_h = 0;
	fb_w = width;
	fb_h = height;
	/* reset the font size as well */
	font_uninit();
	if(font_init(prefs->font_size) == TERM_FAILURE){
		fprintf(stderr, "Couldn't initialize font\n");
		exit_application = 1;
	}

	setup_screen_size(width, height);
	if(virtualkeyboard_visible){
		vkb_h = platform_vkb_height(g_platform);
		setup_screen_size(width, height - vkb_h);
	}
	mark_screen_dirty(1);
	/* Repaint synchronously now instead of waiting for the render thread
	 * to wake on the next event. Twice: the window is double-buffered, so a
	 * single full repaint refreshes only one of the two buffers and the
	 * next post would briefly show the stale (old-size) buffer. We hold
	 * lock_input() in every rescreen() caller, so this cannot race the
	 * render thread (it also renders only under that lock). */
	render_ghostty(1);
	render_ghostty(1);
}

/* Change the active font size at runtime. Clamps to a sane range, then reuses
 * the existing rescreen() teardown/rebuild path (font_uninit -> font_init ->
 * setup_screen_size -> ghostty/PTY resize -> redraw). Session-only: the new
 * size is NOT written back to the config. */
void set_font_size(int new_size){
	if(new_size < MIN_FONT_SIZE)        new_size = MIN_FONT_SIZE;
	if(new_size > TERM_MAX_FONT_SIZE)   new_size = TERM_MAX_FONT_SIZE;
	/* The `term` table is inert until the runtime is up: term.font_size_set
	 * called at .term49.lua load time is a no-op, NOT a way to set the
	 * startup size. The declarative startup size is the `font_size` scalar
	 * global (PREFS_SCALARS -> prefs->font_size -> font_init at startup).
	 * (At reload time the runtime IS ready, so a top-level term.* there acts
	 * on the about-to-be-replaced prefs and self-corrects on the reload's
	 * own rescreen -- harmless; put live tweaks in keybinding functions.) */
	if(!term_runtime_ready()) return;
	if(new_size == prefs->font_size) return;
	prefs->font_size = new_size;
	rescreen(-1, -1);
}

/* Narrow glue for the Lua `term` table (registered in preferences.c).
 * Kept here so preferences.c never sees app/renderer internals. */
int term_current_font_size(void){
	return prefs ? prefs->font_size : TERM_DEFAULT_FONT_SIZE;
}

/* The runtime is "ready" once the app, video surface, and prefs all
 * exist. Until then (notably while .term49.lua is executing inside the
 * startup loader) the global prefs/g_app/screen are still NULL, so every
 * term.* C entry point must bail before dereferencing them. */
int term_runtime_ready(void){
	return g_app != NULL && renderer != NULL && prefs != NULL;
}

int app_run_action_string(const char *s){
	action_t a;
	if(!term_runtime_ready()) return 0;
	if(s == NULL || !action_parse(s, &a)) return 0;
	return app_dispatch_action(g_app, &a);
}

/* Re-run .term49.lua and re-apply it live. MUST be called only from the
 * deferred safe point in the run loop (never from action dispatch / a
 * lua_pcall): the loader closes and reopens the lua_State, and frees the
 * old keymaps/symmenus a keypress may still be unwinding through.
 *
 * In-place move: the loader builds a fresh pref_t; we free the old
 * owned members and overwrite *prefs, keeping the global `prefs`
 * pointer stable so borrowers (app/io/renderer) stay valid. Transient
 * UI pointers into the old prefs are reset first. tty_encoding is not
 * re-applied (io converter is opened once at startup); changing it
 * still needs a restart. */
static void app_reload_config(void){
	pref_t *fresh = prefs_lua_reload();
	if(fresh == NULL){
		/* Parse error / OOM: prefs_lua_reload() left the running config
		 * and scripting state fully intact, so a broken edit can't wipe
		 * a working setup to defaults. No transient on-screen cue: this
		 * backend only composites on the next event pump, so a flash
		 * would not show until an unrelated tap (confusing). The
		 * feedback is simply that nothing changes -- the rejected edit
		 * is logged to stderr for dev builds. */
		return;
	}
	/* Drop transient UI state derived from / pointing into the config
	 * we're about to free. Exhaustive: these are the only globals tied to
	 * the outgoing prefs (renderer symmenu caches are rebuilt below; app/io
	 * borrow only the stable struct pointer; event-handler menu/keymap
	 * pointers are stack locals, gone by this deferred safe point). */
	current_symmenu = NULL;
	metamode = 0;
	altsym_lock = 0;
	symmenu_lock = 0;

	destroy_preferences_members(prefs);  /* free old arrays, keep struct */
	*prefs = *fresh;                     /* move new data into stable struct */
	free(fresh);                         /* free only the empty container */

	/* rebuild derived state from the new prefs */
	if(renderer != NULL){
		renderer_init_symmenus(renderer, prefs);
	}
	rescreen(-1, -1);                    /* font/grid/PTY + synchronous redraw */
}

void toggle_vkeymod(int mod){
	PRINT(stderr, "Toggle modifier %d\n", mod);
	if(vmodifiers & mod){
		vmodifiers &= ~mod;
	}
	else {
		vmodifiers |= mod;
	}
	mark_screen_dirty(1);
}

int app_dispatch_action(app_t *app, const action_t *action) {
	session_t *session;

	if (action == NULL) {
		return 0;
	}

	/* Session-scoped actions go to their target session (0 => active).
	 * Every parsed keybinding resolves to the active session today;
	 * control/scripting (#5) and TAB_* (#4) set a real id. App/window
	 * -scoped builtins stay here. */
	session = app_session_by_id(app, action->target.session);

	switch (action->kind) {
	case TERM_ACTION_SEND_BYTES:
	case TERM_ACTION_SEND_TERMINFO:
		return session_dispatch_action(session, action);
	case TERM_ACTION_BUILTIN:
		switch (action->as.builtin.id) {
		case TERM_BUILTIN_ALT_DOWN:
			toggle_vkeymod(KEYMOD_ALT);
			return 1;
		case TERM_BUILTIN_CTRL_DOWN:
			toggle_vkeymod(KEYMOD_CTRL);
			return 1;
		case TERM_BUILTIN_RESCREEN:
			rescreen(-1, -1);
			return 1;
		case TERM_BUILTIN_PASTE_CLIPBOARD:
			return session_dispatch_action(session, action);
		case TERM_BUILTIN_FONT_SIZE_INCREASE:
			set_font_size(prefs->font_size + 1);
			return 1;
		case TERM_BUILTIN_FONT_SIZE_DECREASE:
			set_font_size(prefs->font_size - 1);
			return 1;
		case TERM_BUILTIN_FONT_SIZE_RESET:
			set_font_size(TERM_DEFAULT_FONT_SIZE);
			return 1;
		case TERM_BUILTIN_LUA_CALL:
			return prefs_lua_invoke(action->as.builtin.arg);
		case TERM_BUILTIN_RELOAD_CONFIG:
			/* Deferred: the loader closes/reopens the lua_State and
			 * frees keymaps this keypress may still be unwinding
			 * through. Applied at the run-loop safe point. */
			g_reload_pending = 1;
			return 1;
		default:
			return 0;
		}
	}

	return 0;
}

static symmenu_t *get_keyhold_actions(int keycode) {
	if (!prefs->keyhold_actions) {
		return NULL;
	}

	int uppercase = 0;
	if (vmodifiers & KEYMOD_SHIFT) {
		uppercase = 1;
	}

	switch (keycode) {
	case KEYCODE_A:
		return prefs->accent_menus[0][uppercase];
	case KEYCODE_B:
		return prefs->accent_menus[1][uppercase];
	case KEYCODE_C:
		return prefs->accent_menus[2][uppercase];
	case KEYCODE_D:
		return prefs->accent_menus[3][uppercase];
	case KEYCODE_E:
		return prefs->accent_menus[4][uppercase];
	case KEYCODE_F:
		return prefs->accent_menus[5][uppercase];
	case KEYCODE_G:
		return prefs->accent_menus[6][uppercase];
	case KEYCODE_H:
		return prefs->accent_menus[7][uppercase];
	case KEYCODE_I:
		return prefs->accent_menus[8][uppercase];
	case KEYCODE_J:
		return prefs->accent_menus[9][uppercase];
	case KEYCODE_K:
		return prefs->accent_menus[10][uppercase];
	case KEYCODE_L:
		return prefs->accent_menus[11][uppercase];
	case KEYCODE_M:
		return prefs->accent_menus[12][uppercase];
	case KEYCODE_N:
		return prefs->accent_menus[13][uppercase];
	case KEYCODE_O:
		return prefs->accent_menus[14][uppercase];
	case KEYCODE_P:
		return prefs->accent_menus[15][uppercase];
	case KEYCODE_Q:
		return prefs->accent_menus[16][uppercase];
	case KEYCODE_R:
		return prefs->accent_menus[17][uppercase];
	case KEYCODE_S:
		return prefs->accent_menus[18][uppercase];
	case KEYCODE_T:
		return prefs->accent_menus[19][uppercase];
	case KEYCODE_U:
		return prefs->accent_menus[20][uppercase];
	case KEYCODE_V:
		return prefs->accent_menus[21][uppercase];
	case KEYCODE_W:
		return prefs->accent_menus[22][uppercase];
	case KEYCODE_X:
		return prefs->accent_menus[23][uppercase];
	case KEYCODE_Y:
		return prefs->accent_menus[24][uppercase];
	case KEYCODE_Z:
		return prefs->accent_menus[25][uppercase];
	}

	return NULL;
}

/* App-layer keyboard handler. Moved verbatim from the old handleKeyboardEvent
 * body (screen_val -> k->sym, the screen_flags KEY_DOWN/KEY_REPEAT bits decoded
 * to k->pressed/k->repeat at the platform boundary, tty writes ->
 * session_write_text) so device behavior is unchanged. It now runs behind the
 * typed event model: any platform key source builds a TERM_EVENT_KEY and the
 * app routes it here. */
static void app_handle_key(app_t *app, const key_event_t *k)
{
	session_t *session = app_active_session(app);
	int modifiers = k->modifiers;
	int num_chars;
	int vkbd_h;
	int metamode_just_set = 0;
	UChar c[CHARACTER_BUFFER];
	UChar *target = c;
	struct timespec now;
	uint64_t now_t, diff_t, metamode_last_t;
	keymap_t *keymap = NULL;
	int32_t last_len = 0;
	int32_t bs_i = 0;
	size_t upcase_len = 0;
	UChar backspace = 0x8;

	if (k->pressed) {
		PRINT(stderr, "The '%d' key was pressed (modifiers: %d) (char %c) (alt %d)\n", (int)k->sym, modifiers, (char)k->sym, (int)k->alternate_sym);
		fflush(stdout);

		/* if we're toggling metamode on or off with doubletap */
		if((k->sym == metamode_doubletap_key) && !k->repeat){
			clock_gettime(CLOCK_MONOTONIC, &now);
			now_t = timespec2nsec(&now);
			metamode_last_t = timespec2nsec(&metamode_last);
			diff_t = now_t > metamode_last_t ? now_t - metamode_last_t : now_t;
			if(diff_t <= prefs->metamode_doubletap_delay){
				metamode_toggle();
				metamode_just_set = 1;
			}
			metamode_last = now;
		}

		/* handle sticky keys */
		if(k->sym == KEYCODE_BB_SYM_KEY){
			if(!k->repeat){
				symmenu_toggle(prefs->main_symmenu);
			} else{
				/* they are holding it down */
				symmenu_stick();
			}
			return;
		}

		if(k->sym == KEYCODE_BB_ALT_KEY){
			if (prefs->sticky_alt_key) {
				if(k->repeat){
					return;
				} else {
					altsym_toggle();
					return;
				}
			}
		}

		if(!virtualkeyboard_visible
		   && ((k->sym == KEYCODE_LEFT_SHIFT) || (k->sym == KEYCODE_RIGHT_SHIFT))){
			if (prefs->sticky_shift_key) {
				if(k->repeat){
					return;
				} else {
					toggle_vkeymod(KEYMOD_SHIFT);
					return;
				}
			}
		}

		/* metamode sticky keys don't trigger repreat */
		if (metamode && !metamode_just_set) {
			keymap = keymap_lookup((char)k->sym, prefs->metamode_sticky_keys);
			if (keymap != NULL){
				app_dispatch_action(app, &keymap->action);
				return;
			}
		}

		/* handle key repeat to upcase / metamode */
		if (k->repeat &&
		    prefs->keyhold_actions &&
		    !is_int_member(prefs->keyhold_actions_exempt, k->sym)) {
			if (!key_repeat_done) {
				/* Check for a metamode toggle key first */
				if (k->sym == prefs->metamode_hold_key) {
					session_write_text(session, &backspace, 1);
					metamode_toggle();
					key_repeat_done = 1;
					return;
				}

				symmenu_t *menu = get_keyhold_actions(k->sym);
				if (menu == NULL) {
					return;
				}

				last_len = io_upcase_last_write(&target, CHARACTER_BUFFER);
				/* write backspace */
				for(bs_i = 1; bs_i <= last_len; ++bs_i) {
					session_write_text(session, &backspace, 1);
				}

				/* select the mapping */

				// uppercase automatically if there's no accents or accents disabled
				if ((menu->entries[1].to == NULL) || (!prefs->keyhold_accents)) {
					/* We can upcase, send last_len backspaces and then the upcase char.
					 * Note that this really only works if the program on the other
					 * end of the line understands unicode, and can marry up backspaces
					 * with codepoints, instead of just blindly deleting one byte at a time. */
					app_dispatch_action(app, &menu->entries[0].action);
				} else {
					symmenu_toggle(menu);
				}
				key_repeat_done = 1;
				return;
			} else {
				// We have already handled this key repeat
				return;
			}
		} else {
			key_repeat_done = 0;
		}

		if(metamode && !metamode_just_set){
			keymap = keymap_lookup((char)k->sym, prefs->metamode_keys);
			if(keymap != NULL){
				app_dispatch_action(app, &keymap->action);
				metamode_toggle();
				return;
			}
			// else
			keymap = keymap_lookup((char)k->sym, prefs->metamode_func_keys);
			if(keymap != NULL){
				app_dispatch_action(app, &keymap->action);
			}
			metamode_toggle();
			return;
		}

		/* handle alt keys */
		if (altsym_lock) {
			keymap = keymap_lookup((char)k->sym, prefs->altsym_entries);
			altsym_toggle();
			if (keymap != NULL){
				app_dispatch_action(app, &keymap->action);
				return;
			}
		}

		/* handle sym keys */
		if (current_symmenu != NULL) {
			keymap = keymap_lookup((char)k->sym, current_symmenu->entries);
			if (keymap != NULL){
				app_dispatch_action(app, &keymap->action);
				symmenu_toggle(NULL);
				return;
			}
		}

		/* if we have virtual keymods, then put them in, then turn them off */
		modifiers |= vmodifiers;
		if (vmodifiers != 0) {
			/* the on-screen ctrl/alt/shift indicators reflect vmodifiers
			 * clearing them changes the frame, so the
			 * dirty gate must repaint even if this key emits nothing
			 * visible itself -- otherwise a stale indicator lingers. */
			mark_screen_dirty(1);
		}
		vmodifiers = 0;

		/* now process the keypress */
		switch (k->sym) {
		case KEYCODE_PAUSE      :
		case KEYCODE_SCROLL_LOCK:
		case KEYCODE_PRINT      :
		case KEYCODE_SYSREQ     :
		case KEYCODE_BREAK      :
			//case KEYCODE_ESCAPE     :
			//case KEYCODE_BACKSPACE  :
			//case KEYCODE_TAB        :
			//case KEYCODE_BACK_TAB   :
		case KEYCODE_LEFT_ALT   :
		case KEYCODE_RIGHT_ALT  :
		case KEYCODE_LEFT_SHIFT :
		case KEYCODE_RIGHT_SHIFT:
		case KEYCODE_MENU       :
			//case KEYCODE_INSERT     :
			//case KEYCODE_HOME       :
			//case KEYCODE_PG_UP      :
			//case KEYCODE_DELETE     :
			//case KEYCODE_END        :
			//case KEYCODE_PG_DOWN    :
		case KEYCODE_NUM_LOCK   :
			//case KEYCODE_F1         :
			//case KEYCODE_F2         :
			//case KEYCODE_F3         :
			//case KEYCODE_F4         :
			//case KEYCODE_F5         :
			//case KEYCODE_F6         :
			//case KEYCODE_F7         :
			//case KEYCODE_F8         :
			//case KEYCODE_F9         :
			//case KEYCODE_F10        :
			//case KEYCODE_F11        :
			//case KEYCODE_F12        :
			PRINT(stderr, "Modifier %d\n", k->sym);
			break;
		case KEYCODE_LEFT_CTRL  :
		case KEYCODE_RIGHT_CTRL :
			toggle_vkeymod(KEYMOD_CTRL);
			break;
		case KEYCODE_LEFT_HYPER :
		case KEYCODE_RIGHT_HYPER:
			toggle_vkeymod(KEYMOD_CTRL);
			break;
		case KEYCODE_CAPS_LOCK  :
			toggle_vkeymod(KEYMOD_CTRL);
			break;
		default:
			num_chars = terminal_key_sequence(k->sym, modifiers, c);
			int nc;
			for(nc = 0; nc < num_chars; ++nc){
				PRINT(stderr, "Writing 0x%x\n", (int)c[nc]);
			}
			session_write_text(session, (const UChar*)&c, num_chars);
			break;
		}
	}
}

void set_tty_window_size(){
	int master = io_get_master();
	struct winsize ws;

	memset(&ws, 0, sizeof(ws));
	ws.ws_row = rows;
	ws.ws_col = cols;
	ws.ws_xpixel = fb_w;
	ws.ws_ypixel = fb_h;

	if(tcsetsize(master, rows, cols) < 0){
		PRINT(stderr, "ERROR: tcsetsize() returned <0 (%s). Did not set child pty window size. \n", strerror(errno));
	}
	if(ioctl(master, TIOCSWINSZ, &ws) < 0){
		PRINT(stderr, "ERROR: TIOCSWINSZ returned <0 (%s). Did not set child pty window size. \n", strerror(errno));
	}

	/* Send SIGWINCH to the shell's process group. TIOCGPGRP on the pty
	 * master fails on BB10/QNX, so fall back to the child process group
	 * created by setsid() in pty_init(). Sending SIGWINCH to our own app
	 * process group leaves mksh thinking it is still 80 columns wide, which
	 * causes broken prompt/readline wrapping until something like tmux fixes
	 * the size for its inner pty. */
	int pgrp;
	if(ioctl(master, TIOCGPGRP, &pgrp) != -1){
		killpg(pgrp, SIGWINCH);
	} else if(child_pid > 0){
		PRINT(stderr, "Could not get pgrp of tty: %s; using child pgid %d\n", strerror(errno), child_pid);
		if(killpg(child_pid, SIGWINCH) < 0){
			kill(child_pid, SIGWINCH);
		}
	} else {
		PRINT(stderr, "Could not get pgrp of tty and no child exists: %s\n", strerror(errno));
	}
}

/* Call _after_ we have calculated the text size */
void setup_screen_size(int s_w, int s_h){

	if(s_w <= 1 || s_h <= 1){
		/* refusing to do that */
		return;
	}

	rows = s_h / text_height;
	cols = s_w / text_width;
	PRINT(stderr, "Rows: %d Cols: %d\n", rows, cols);

	set_tty_window_size();
	ghostty_bridge_resize((uint16_t)cols, (uint16_t)rows,
	                      (uint32_t)advance, (uint32_t)text_height);
}

void lock_input(){
	pthread_mutex_lock(&input_mutex);
}
void unlock_input(){
	pthread_mutex_unlock(&input_mutex);
}

void indicate_event_input(){
	char *indicate_buf = "w";
	/* indicate that the render thread should run. Note that
	 * we are logging errors here, but aren't doing anything with them. */
	if(write(event_pipe[1], (void*)indicate_buf, 1) < 0){
		fprintf(stderr, "Error writing to event pipe: %d\n", errno);
	}
}


/* This function is intended for resizing the number of
 * colums after app init */
void set_screen_cols(int ncols){
	/* the user wants this number of columns */
	if (prefs->allow_resize_columns) {
		int new_fontsize = preferences_guess_best_font_size(prefs, ncols);
		font_uninit();
			if(font_init(new_fontsize) == TERM_FAILURE){
			fprintf(stderr, "Error setting new font size\n");
			exit_application = 1;
		} else {
			setup_screen_size(fb_w, fb_h);
			/* and force the number of columns */
			cols = ncols;
			set_tty_window_size();
			ghostty_bridge_resize((uint16_t)cols, (uint16_t)rows,
			                      (uint32_t)advance, (uint32_t)text_height);
			mark_screen_dirty(1);
		}
	}
}

static int startup_init() {
	pthread_mutex_init(&input_mutex, NULL);
	input_mutex_inited = 1;

	if(pipe(event_pipe) == -1){
		fprintf(stderr, "Couldn't create event pipe\n");
		return TERM_FAILURE;
	}
	event_pipe_open = 1;

	if(font_library_init() != 0){
		fprintf(stderr, "Couldn't initialize FreeType\n");
		return TERM_FAILURE;
	}
	font_library_inited = 1;

	if(font_init(prefs->font_size) == TERM_FAILURE){
		PRINT(stderr, "Couldn't initialize font\n");
		return TERM_FAILURE;
	}
	font_inited = 1;

	renderer = renderer_screen_create(g_platform, font);
	if (renderer == NULL) {
		fprintf(stderr, "Couldn't create renderer\n");
		return TERM_FAILURE;
	}

	if(renderer_framebuffer_size(renderer, &fb_w, &fb_h) != 0 || fb_w <= 0 || fb_h <= 0){
		fprintf(stderr, "Couldn't determine framebuffer size\n");
		return TERM_FAILURE;
	}

	rows = fb_h / text_height;
	cols = fb_w / text_width;

	setup_screen_size(fb_w, fb_h);

	clock_gettime(CLOCK_MONOTONIC, &metamode_last);

	if(ghostty_bridge_init((uint16_t)cols, (uint16_t)rows, 1000) != 0){
		fprintf(stderr, "Unable to initialize libghostty-vt terminal\n");
		return TERM_FAILURE;
	}
	ghostty_inited = 1;
	ghostty_bridge_resize((uint16_t)cols, (uint16_t)rows,
	                      (uint32_t)advance, (uint32_t)text_height);

	return TERM_SUCCESS;
}

void app_shutdown(void){

	/* Every cleanup below is gated on the matching init flag so the
	 * early-exit paths in main() (io_init / pty_init / sigaction / startup_init
	 * failures) don't call destructors on resources that were never set up. */

	if (input_mutex_inited) {
		pthread_mutex_destroy(&input_mutex);
		input_mutex_inited = 0;
	}

	/* Tear down session state before ghostty_bridge_uninit()/io_uninit()
	 * below, since the single session borrows both. NULL-safe on the
	 * pre-renderer early-exit paths where g_app was never created. */
	app_shutdown_state(g_app);
	g_app = NULL;

	/* Order matters: free the renderer first so its glyph cache (which
	 * borrows the font) drops before font_uninit closes the font. Then
	 * tear down the font and the FreeType library, then the platform. */
	renderer_destroy(renderer);
	renderer = NULL;

	if (font_inited) {
		font_uninit();
		font_inited = 0;
	}
	if (font_library_inited) {
		font_library_quit();
		font_library_inited = 0;
	}

	platform_destroy(g_platform);
	g_platform = NULL;

	if (ghostty_inited) {
		ghostty_bridge_uninit();
		ghostty_inited = 0;
	}

	if (event_pipe_open) {
		close(event_pipe[0]);
		close(event_pipe[1]);
		event_pipe_open = 0;
	}

	/* prefs is assigned (or the process exit(1)s) before any app_shutdown()
	 * call site, so it is always non-NULL here. */
	prefs_lua_destroy(prefs);

	io_uninit();
}

static rgb_t to_rgb(ghostty_bridge_rgb_t c) {
	rgb_t out;
	out.r = c.r;
	out.g = c.g;
	out.b = c.b;
	return out;
}

struct ghostty_render_context {
	ghostty_bridge_frame_t frame;
	int failed;
};

static void render_ghostty_cell(uint16_t x, uint16_t y,
                                const ghostty_bridge_cell_t *cell,
                                void *userdata) {
	struct ghostty_render_context *ctx = (struct ghostty_render_context *)userdata;

	if (ctx == NULL || cell == NULL || x >= cols || y >= rows) { return; }

	rgb_t fg = to_rgb(cell->has_fg ? cell->fg : ctx->frame.default_fg);
	rgb_t bg = to_rgb(cell->has_bg ? cell->bg : ctx->frame.default_bg);

	if (cell->inverse || flash ||
	    (draw_cursor && ctx->frame.cursor_visible &&
	     x == ctx->frame.cursor_x && y == ctx->frame.cursor_y)) {
		rgb_t tmp = fg;
		fg = bg;
		bg = tmp;
	}

	rect_t destrect;
	destrect.x = x * advance;
	destrect.y = y * text_height;
	destrect.w = advance;
	destrect.h = text_height;
	renderer_fill_rect(renderer, &destrect, bg);

	if (!cell->has_text || cell->codepoint == 0 || cell->wide_tail || cell->invisible) {
		return;
	}

	font_style_t style = FONT_STYLE_NORMAL;
	if (cell->bold)      { style |= FONT_STYLE_BOLD; }
	if (cell->italic)    { style |= FONT_STYLE_ITALIC; }
	if (cell->underline) { style |= FONT_STYLE_UNDERLINE; }

	if (renderer_draw_glyph(renderer, destrect.x, destrect.y,
	                        cell->codepoint, style, fg, bg) != 0) {
		PRINT(stderr, "Ghostty glyph render failed for U+%04x\n",
		      (unsigned)cell->codepoint);
		ctx->failed = 1;
	}
}

static int render_ghostty(int force_full_repaint) {
	static int prev_cursor_valid = 0;
	static int prev_cursor_visible = 0;
	static uint16_t prev_cursor_x = 0;
	static uint16_t prev_cursor_y = 0;
	struct ghostty_render_context ctx;
	int cursor_visible;
	int cursor_changed;

	if (ghostty_bridge_begin_frame(&ctx.frame) != 0) {
		fprintf(stderr, "ghostty render: begin_frame failed\n");
		return 0;
	}
	if (ctx.frame.cursor_wide_tail && ctx.frame.cursor_x > 0) {
		ctx.frame.cursor_x -= 1;
	}
	ctx.failed = 0;
	cursor_visible = draw_cursor && ctx.frame.cursor_visible;
	cursor_changed = prev_cursor_valid &&
		(prev_cursor_visible != cursor_visible ||
		 prev_cursor_x != ctx.frame.cursor_x ||
		 prev_cursor_y != ctx.frame.cursor_y);

	/* Force every repaint to be a full repaint. The native Screen backend
	 * is double-buffered (screen_create_window_buffers(2)) and we have no
	 * per-buffer damage tracking: a partial paint to buffer A leaves B
	 * stale, so on the next flip the previous frame's content reappears,
	 * producing flicker and a phantom cursor at the old position. Until
	 * each buffer tracks its own dirty set, just paint everything every
	 * frame — the terminal is small enough that this is cheap. */
	force_full_repaint = 1;

	renderer_begin_frame(renderer);

	rgb_t bg = to_rgb(ctx.frame.default_bg);
	if (force_full_repaint) {
		renderer_clear(renderer, bg);
	}
	if (force_full_repaint || ctx.frame.dirty != 0) {
		if (ghostty_bridge_visit_cells(!force_full_repaint, render_ghostty_cell, &ctx) != 0 || ctx.failed) {
			fprintf(stderr, "ghostty render: visit_cells failed\n");
			renderer_end_frame(renderer);
			return 0;
		}
	}
	if (!force_full_repaint && cursor_changed) {
		if (prev_cursor_visible && prev_cursor_y < rows &&
		    ghostty_bridge_visit_row(prev_cursor_y, render_ghostty_cell, &ctx) != 0) {
			fprintf(stderr, "ghostty render: visit old cursor row failed\n");
			renderer_end_frame(renderer);
			return 0;
		}
		if (cursor_visible && ctx.frame.cursor_y < rows &&
		    (!prev_cursor_visible || prev_cursor_y != ctx.frame.cursor_y) &&
		    ghostty_bridge_visit_row(ctx.frame.cursor_y, render_ghostty_cell, &ctx) != 0) {
			fprintf(stderr, "ghostty render: visit new cursor row failed\n");
			renderer_end_frame(renderer);
			return 0;
		}
	}
	prev_cursor_valid = 1;
	prev_cursor_visible = cursor_visible;
	prev_cursor_x = ctx.frame.cursor_x;
	prev_cursor_y = ctx.frame.cursor_y;

	if (metamode && metamode_cursor != NULL) {
		renderer_draw_bitmap(renderer, (cols - 1) * advance, 0, metamode_cursor);
	}
	if (vmodifiers & KEYMOD_CTRL) {
		renderer_draw_bitmap(renderer, (cols - 1) * advance, 1 * text_height, ctrl_key_indicator);
	}
	if (vmodifiers & KEYMOD_ALT) {
		renderer_draw_bitmap(renderer, (cols - 1) * advance, 2 * text_height, alt_key_indicator);
	}
	if (vmodifiers & KEYMOD_SHIFT) {
		renderer_draw_bitmap(renderer, (cols - 1) * advance, 3 * text_height, shift_key_indicator);
	}
	if (altsym_lock) {
		renderer_draw_bitmap(renderer, (cols - 1) * advance, 3 * text_height, altsym_indicator);
	}

	const bitmap_t *symmenu_surface = renderer_symmenu_surface_for(renderer, current_symmenu);
	if (symmenu_surface != NULL) {
		renderer_draw_bitmap(renderer, 0, fb_h - symmenu_surface->h, symmenu_surface);
	}

	ghostty_bridge_finish_frame();
	renderer_end_frame(renderer);

	if(flash){
		flash = 0;
		mark_screen_dirty(1);
		indicate_event_input();
	}

	return 1;
}

static void terminal_setenv(void) {
	/* terminfo is located via $TERMINFO (an absolute path to the bundled
	 * database, exported in main() before fork). */
	setenv("TERM", "xterm-256color", 1);
	if(system("/base/bin/stty +sane erase=^H") == -1){
		PRINT(stderr, "Error invoking system(stty..)\n");
	}
}

static int pty_init() {
	// Set up the ttys and fork

	struct winsize winp;

	/* some sensible defaults - we change these later */
	winp.ws_row = 24;
	winp.ws_col = 80;
	winp.ws_xpixel = 1024;
	winp.ws_ypixel = 600;

	int pty_ret;
	int fd;
	int uid = getuid();
	int gid = getgid();
	char cttyname[L_ctermid];
	char envstr[100];
	int slave_fd;
	int master_fd;

	pty_ret = openpty(&master_fd, &slave_fd, slave_ptyname, NULL, &winp);
	if (pty_ret != 0){
		// error
		PRINT(stderr, "openpty returned: %s\n", strerror(errno));
		close(master_fd);
		close(slave_fd);
		return TERM_FAILURE;
	} else {
		PRINT(stderr, "openpty returned name: %s\n", slave_ptyname);
	}

	// turn off blocking on the master pty
	fcntl(master_fd, F_SETFL, fcntl(master_fd, F_GETFL) | O_NONBLOCK);

	// store the master_fd in IO
	io_set_master(master_fd);

	// fork and exec
	child_pid = fork();

	if (child_pid == 0) {
		// Child
		/*
		  struct termios tios;
		  if (tcgetattr(STDIN_FILENO, &tios) >= 0)
		  {
		  tios.c_lflag &= ~(ECHO | ECHOE | ECHOK | ECHONL);
		  tios.c_oflag &= ~(ONLCR);
		  (void) tcsetattr(STDIN_FILENO, TCSANOW, &tios);
		  }
		*/

		PRINT(stderr, "fork returned in child\n");
		ctermid(cttyname);
		PRINT(stderr, "controlling tty is: %s\n", cttyname);

		if(setuid(uid)<0) {PRINT(stderr, "ERROR (setuid)\n");}
		if(setgid(gid)<0) {PRINT(stderr, "ERROR (setgid)\n");}
		if(setsid()<0) {PRINT(stderr, "ERROR (setsid)\n");}

		if(ioctl(slave_fd, TIOCSCTTY, NULL)) {
			PRINT(stderr, "ERROR! (ioctl): %s\n", strerror(errno));
		}

		dup2(slave_fd, STDIN_FILENO);
		dup2(slave_fd, STDOUT_FILENO);
		dup2(slave_fd, STDERR_FILENO);

		terminal_setenv();

		/* add in our private binary path */
		char* home = getenv("SANDBOX");
		char* path = getenv("PATH");
		char* root = "app/native/root/bin";
		char* newpath;
		int err = 0;
		int newpath_len = 0;
		if(home == NULL || path == NULL){
			fprintf(stderr, "Could not get $HOME or $PATH - not setting private bin dir.\n");
		} else {
			newpath_len = strlen(home) + strlen(path) + strlen(root) + 10;
			newpath = calloc(newpath_len, sizeof(char));
			if(newpath == NULL){
				fprintf(stderr, "Could not calloc new $PATH - not setting private bin dir..\n");
			} else {
				err = snprintf(newpath, newpath_len, "PATH=%s/%s:%s\n", home, root, path);
				if(err > 0){
					err = putenv(strdup(newpath));
					if(err < 0){
						fprintf(stderr, "Error in putenv: %d\n%s - private bin may not bein $PATH.\n", errno, newpath);
					}
				} else {
					fprintf(stderr, "Error snprintf setting $PATH: %d\n", errno);
				}
				free(newpath);
			}
		}

		/* Set LC_CTYPE=en_US.UTF-8
		 * Which can be overridden in .profile */
		setenv("LC_CTYPE", "en_US.UTF-8", 0);
		/* mksh lives at $SANDBOX/app/native/root/bin/mksh. Use an
		 * absolute path: CWD is now the shared HOME, so the old
		 * "../app/native/..." relative path no longer resolves. */
		char mksh[1024];
		if(home != NULL &&
		   snprintf(mksh, sizeof(mksh), "%s/%s/mksh", home, root) < (int)sizeof(mksh)){
			execl(mksh, "mksh", "-l", (char*)0);
		}
		if(execl("../app/native/root/bin/mksh", "mksh", "-l", (char*)0) == -1){
			execl("/bin/sh", "sh", "-l", (char*)0);
		}
	}
	if (child_pid == -1){
		PRINT(stderr, "fork returned: %s\n", strerror(errno));
		return TERM_FAILURE;
	}

	// close the slave_fd, not needed anymore
	close(slave_fd);
	return TERM_SUCCESS;
}

void sig_child(int signo){
	int status;

	int old_errno = errno;

	if(waitpid(child_pid, &status, WNOHANG)){
		if(WIFEXITED(status)){
			PRINT(stderr, "Child %d exited normally with status %d\n", child_pid, WEXITSTATUS(status));
		} else {
			PRINT(stderr, "Child %d exited abnormally\n", child_pid);
		}
		exit_application = 1;
		/* Poke the event pipe so the render thread's select() unblocks and
		 * sees exit_application set. The main run loop is woken by the next
		 * BPS event; for a faster shutdown a follow-up could push a user
		 * BPS event here, but the existing flow is correct. */
		if (event_pipe[1] >= 0) {
			char w = 'q';
			(void)write(event_pipe[1], &w, 1);
		}
	} else {
		PRINT(stderr, "Got SIGCHILD for process other than %d\n", child_pid);
	}
	errno = old_errno;
}

/* Runs in a dedicated pthread. Blocks in select() on the pty master and
 * the event pipe; either input triggers a dirty-gate check and, if set,
 * a render pass.
 */
void *run_render(void *data){

	fd_set fds;
	char ev_buf[100];
	int n = 0;
	char rawbuf[READ_BUFFER_SIZE];
	ssize_t num_bytes = 0;
	int master = io_get_master();
	while(!exit_application){
		FD_ZERO(&fds);
		FD_SET(master, &fds);
		FD_SET(event_pipe[0], &fds);
		n = select(1+max(master, event_pipe[0]), &fds, NULL, NULL, NULL);
		if(n < 0){
			printf("Error calling select on inputs: %d\n", errno);
		} else {
			if(FD_ISSET(master, &fds)){
				lock_input();
				// Feed raw VT bytes directly to libghostty-vt.
				while ((num_bytes = io_read_master_raw(rawbuf, READ_BUFFER_SIZE)) > 0){
					ghostty_bridge_write((const uint8_t*)rawbuf, (size_t)num_bytes);
				}
				/* child produced output -> terminal rows changed; let Ghostty's
				 * render-state dirty map decide whether this is full or partial. */
				mark_screen_dirty(0);
				unlock_input();
			}
			if(FD_ISSET(event_pipe[0], &fds)){
				// Just read the stuff and throw it away
				read(event_pipe[0], (void*)ev_buf, 99);
			}
		}
		/* Only repaint when something visible actually changed. The pipe
		 * poke wakes us for every platform event, but inert events
		 * (orientation check, ignored touch, unknown) leave screen_dirty
		 * clear, so we skip the full-screen clear + post that was causing
		 * the white-flash storm. */
		lock_input();
		int do_render = screen_dirty;
		int force_full_repaint = screen_full_dirty;
		screen_dirty = 0;
		screen_full_dirty = 0;
		unlock_input();

		if(do_render){
			PRINT(stderr, "Render Loop\n");
			lock_input();
			render_ghostty(force_full_repaint);
			unlock_input();
		}
	}
	return NULL;
}

int app_handle_event(app_t *app, const event_t *event) {
	if (event == NULL) {
		return 0;
	}

	switch (event->type) {
	case TERM_EVENT_QUIT:
		exit_application = 1;
		return 1;
	case TERM_EVENT_RESIZE:
		/* Apply any pending platform-side geometry change (rotation: set
		 * ROTATION/SIZE/SOURCE_SIZE and destroy+recreate the render
		 * buffers) *before* rescreen reflows ghostty and re-paints. This
		 * runs under input_mutex (lock_input held by the caller), so the
		 * destructive buffer rebuild can't race the render thread. */
		platform_apply_pending_resize(g_platform);
		rescreen(event->as.resize.w, event->as.resize.h);
		mark_screen_dirty(1);
		return 1;
	case TERM_EVENT_KEY:
		if (event->as.key.pressed) {
			/* Any keypress snaps the viewport back to the live bottom
			 * so the keystroke isn't typed into history. libghostty
			 * no-ops when already at bottom. */
			ghostty_bridge_scroll_to_bottom();
		}
		app_handle_key(app, &event->as.key);
		return 1;
	case TERM_EVENT_TOUCH_DOWN:
		drag_reset();
		g_drag.active  = 1;
		g_drag.start_y = (int16_t)event->as.touch.y;
		g_drag.last_y  = (int16_t)event->as.touch.y;
		/* Lock the gesture out of scroll mode if a symmenu is already
		 * open or we're in alt-screen (vim/less own scrolling). The
		 * latch persists across the gesture so a symmenu tap that
		 * dismisses the menu can't then scroll mid-finger-stroke. */
		if (current_symmenu != NULL || ghostty_bridge_is_alt_screen()) {
			g_drag.locked = 1;
		}
		handle_mousedown(event->as.touch.x, event->as.touch.y);
		mark_screen_dirty(1);
		return 1;
	case TERM_EVENT_TOUCH_MOVE: {
		if (!g_drag.active || g_drag.locked) {
			return 1;
		}
		int16_t y = (int16_t)event->as.touch.y;
		int dy = (int)y - (int)g_drag.last_y;
		g_drag.last_y = y;
		if (!g_drag.committed) {
			if (abs((int)y - (int)g_drag.start_y) < text_height / 2) {
				return 1;
			}
			g_drag.committed = 1;
		}
		g_drag.accum_dy += dy;
		int rows = g_drag.accum_dy / text_height;
		if (rows != 0) {
			g_drag.accum_dy -= rows * text_height;
			/* Natural / iOS-style: finger-down (rows > 0) reveals older
			 * content. libghostty uses "up is negative" for its scroll
			 * delta, so we negate to go into history on finger-down. */
			ghostty_bridge_scroll_view(-rows);
			mark_screen_dirty(1);
		}
		return 1;
	}
	case TERM_EVENT_TOUCH_UP:
		if (g_drag.active && !g_drag.committed && !g_drag.locked) {
			maybe_show_vkb();
		}
		drag_reset();
		return 1;
	case TERM_EVENT_ACTIVATE:
		handle_activeevent(event->as.activate.active, event->as.activate.state);
		return 1;
	case TERM_EVENT_VKB:
		{
			int vis = event->as.vkb.visible;
			int vkb_h;
			if (vis >= 0) {
				/* explicit show/hide */
				virtualkeyboard_visible = (char)vis;
				vkb_h = vis ? platform_vkb_height(g_platform) : 0;
			} else {
				/* height-only INFO update: keep current visibility */
				vkb_h = virtualkeyboard_visible ? event->as.vkb.height : 0;
			}
			setup_screen_size(fb_w, fb_h - vkb_h);
			/* rows/cols changed -> next frame must repaint so the new
			 * effective viewport is reflected (matches the dirty-mark in
			 * TERM_EVENT_RESIZE). */
			mark_screen_dirty(1);
		}
		return 1;
	case TERM_EVENT_NONE:
	default:
		return 0;
	}
}

int main(int argc, char **argv) {
	int rc;

	/* Redirect HOME to a persistent, externally-visible dir before
	 * anything reads it (this function, the chdir below, the shell
	 * we later fork, and preferences.c all inherit the new value). */
	set_persistent_home();

	/* Switch to our home directory */
	char* home = getenv("HOME");
	if(home != NULL){ chdir(home); }

	/* Load prefs FIRST so the screen-idle decision is in hand before we
	 * create the native window (platform_screen_create reads
	 * SCREEN_IDLE_NORMAL when setting SCREEN_PROPERTY_IDLE_MODE). */
	int lua_cfg_existed = (access(PREFS_LUA_FILE_PATH, F_OK) == 0);
	prefs = prefs_lua_load(PREFS_LUA_FILE_PATH);
	if (!lua_cfg_existed) {
		prefs_first_run_readme();
		prefs_emit_lua(prefs, PREFS_LUA_FILE_PATH);
	}
	if (!prefs->screen_idle_awake) {
		setenv("SCREEN_IDLE_NORMAL", "1", 0);
	}
	/* AUTO_ORIENTATION is read by the navigator orientation-check handler at
	 * runtime, so set it before platform_screen_create starts pumping events. */
	setenv("AUTO_ORIENTATION", "1", 0);

	/* Native Screen/BPS platform backend. Owns the window/context/buffers
	 * and the event pump. */
	g_platform = platform_screen_create();
	if (g_platform == NULL) {
		fprintf(stderr, "Unable to initialize Screen/BPS platform\n");
		return TERM_FAILURE;
	}

	if (platform_is_passport(g_platform)) {
		prefs->auto_show_vkb = 1;
	}

	/* Initialize IO */
	if (TERM_SUCCESS != io_init(prefs)) {
		PRINT(stderr, "Unable to initialize IO\n");
		app_shutdown();
		return TERM_FAILURE;
	}
	
	/* Initialize pty */
	if (TERM_SUCCESS != pty_init()) {
		PRINT(stderr, "Unable to initialize pty/tty\n");
		app_shutdown();
		return TERM_FAILURE;
	}

	/* Install signal handler for SIGCHILD */
	struct sigaction act;
	act.sa_handler = &sig_child;
	sigemptyset(&act.sa_mask);
	act.sa_flags = SA_NOCLDSTOP;
	if (sigaction(SIGCHLD, &act, NULL) < 0) {
		PRINT(stderr, "sigaction failed\n");
		app_shutdown();
		return TERM_FAILURE;
	}

	/* initialize FreeType, font, renderer, ghostty bridge. The native
	 * Screen window was already created by platform_screen_create above. */
	if (TERM_SUCCESS != startup_init()) {
		PRINT(stderr, "Unable to initialize startup state\n");
		app_shutdown();
		return TERM_FAILURE;
	}

	if (renderer_init_symmenus(renderer, prefs) != 0) {
		PRINT(stderr, "Unable to initialize renderer symmenu caches\n");
		app_shutdown();
		return TERM_FAILURE;
	}

	/* App state owns the (single) session. Created after pty_init() and
	 * ghostty_bridge_init() (inside startup_init) so the session can adopt
	 * the io master fd + ghostty singleton. */
	if (app_init(&g_app, prefs) != 0) {
		PRINT(stderr, "Unable to initialize app state\n");
		app_shutdown();
		return TERM_FAILURE;
	}

	if (prefs->auto_show_vkb) {
		platform_vkb_show(g_platform);
	}

	/* start up main event loop */
	pthread_t render_thread;
	pthread_create(&render_thread, NULL, run_render, NULL);
	/* screen_dirty starts set for the first frame, but the render thread
	 * blocks in select() until either pty output or a platform event
	 * arrives. Poke it once so launch never sits on an undrawn buffer. */
	indicate_event_input();
	while (!exit_application) {

		//Request and process the next event. platform_next_event blocks
		//in bps_get_event outside the lock; only dispatch is locked. The
		//render-thread poke stays unconditional, as before.
		event_t event;
		int have = platform_next_event(g_platform, &event);

		lock_input();
		if (have) {
			app_handle_event(g_app, &event);
		}
		/* Safe point: the triggering event (and any lua_pcall within
		 * it) has fully returned; still under the input lock, same
		 * thread as rescreen. */
		if (g_reload_pending) {
			g_reload_pending = 0;
			app_reload_config();
		}
		indicate_event_input();
		unlock_input();
	}

	PRINT(stderr, "Exiting run loop\n");
	/* exit_application is already set; poke the event pipe so the render
	 * thread's select() unblocks and sees the flag, then join cleanly. */
	indicate_event_input();
	pthread_join(render_thread, NULL);
	platform_vkb_hide(g_platform);
	app_shutdown();

	return 0;
}
