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

#include "SDL.h"
#include "SDL_ttf.h"
#include "SDL_syswm.h"
#include "SDL_thread.h"

#include "types.h"
#include "terminal.h"
#include "action.h"
#include "prefs.h"
#include "symmenu.h"
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

static char symmenu_lock = 0;
static char altsym_lock = 0;

static char metamode = 0;
static int metamode_doubletap_key = 0;
static struct timespec metamode_last;
static SDL_Color metamode_cursor_fg = T49_COLOR_BLACK;
static SDL_Color metamode_cursor_bg = T49_COLOR_GREEN;
static SDL_Surface* metamode_cursor;
static int vmodifiers = 0;

static TTF_Font* font;
static int text_width;
static int text_height;
static int text_height_padding;
static int advance;

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

static SDL_Surface* screen;
static SDL_Surface* ctrl_key_indicator;
static SDL_Surface* alt_key_indicator;
static SDL_Surface* shift_key_indicator;
static SDL_Surface* altsym_indicator;

static pid_t child_pid = -1;

static char virtualkeyboard_visible = 0;
static char key_repeat_done = 0;

static SDL_mutex *input_mutex = NULL;

static int event_pipe[2];

static int rows;
static int cols;

static void glyph_cache_clear(void);

#define PB_D_PIXELS 32

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
		/* libconfig will return ascii strings, but we can put utf8 in there too */
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

int get_virtualkeyboard_height(){
	int rc, vkb_h;
	rc = virtualkeyboard_get_height(&vkb_h);
	if(rc != BPS_SUCCESS){
		fprintf(stderr, "Could not get virtual keyboard height\n");
		vkb_h = 0; // assume zero?
	}
	return vkb_h;
}

int is_passport() {
	deviceinfo_details_t *di_t = NULL;
	int rc = deviceinfo_get_details(&di_t);
	if(rc != BPS_SUCCESS){
		fprintf(stderr, "Could not get device info");
		return 0;
	}
	
	int passport = 0;
	if(strncmp("Passport", deviceinfo_details_get_model_name(di_t), 8) == 0){
		passport = 1;
	}
	deviceinfo_free_details(&di_t);

	return passport;
}

int get_wm_info(SDL_SysWMinfo* info){
	SDL_version version;
	SDL_VERSION(&version);
	info->version = version;
	return SDL_GetWMInfo(info);
}

/* These local-UI mutators are the funnel for keyboard/touch-driven
 * screen changes that never round-trip the pty (metamode cursor,
 * symmenu overlay, modifier indicators, font reflow). They are reached
 * both from handle_mousedown (SDL main loop) and from handleKeyboardEvent
 * (called directly by the vendored libSDL12 on a screen key event, which
 * the main loop only sees as an inert "Unhandled SYSWMEVENT"). Marking
 * dirty here, at the mutation, is what makes the render gate correct
 * regardless of which SDL event delivered the input. */
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
		current_symmenu = target;
		// resize to show menu
		if (prefs->rescreen_for_symmenu) {
			setup_screen_size(screen->w, screen->h - current_symmenu->surface->h);
		}
		if (prefs->sticky_sym_key) {
			symmenu_stick();
		}
	} else {
		current_symmenu = NULL;
		if (prefs->rescreen_for_symmenu) {
			// resize to take full screen
			setup_screen_size(screen->w, screen->h);
		}
		symmenu_lock = 0;
	}
	mark_screen_dirty(1);
}

static const char* symkey_for_mousedown(symmenu_t *menu, Uint16 x, Uint16 y) {
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
				return key->map->to;
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
	font = TTF_OpenFont(prefs->font_path, font_size);
	if ( font == NULL ) {
		/* try opening the default stuff */
		fprintf(stderr, "Couldn't load %d pt font from %s: %s\n", font_size, prefs->font_path, SDL_GetError());
		font = TTF_OpenFont(T49_DEFAULT_FONT_PATH, T49_DEFAULT_FONT_SIZE);
		if(font == NULL){
			fprintf(stderr, "Could not open default font %s: %s\n", T49_DEFAULT_FONT_PATH, SDL_GetError());
			return TERM_FAILURE;
		}
	}
	PRINT(stderr, "Font is Fixed Width: %d\n", TTF_FontFaceIsFixedWidth(font));

	/* Set default options */
	TTF_SetFontStyle(font, TTF_STYLE_NORMAL);
	TTF_SetFontOutline(font, 0);
	TTF_SetFontKerning(font, 0);
	TTF_SetFontHinting(font, TTF_HINTING_NORMAL);

	/* initialize modifier indicator glyphs */
	UChar str[2] = {'A', 0};
	alt_key_indicator = TTF_RenderUNICODE_Shaded(font, str, metamode_cursor_fg, metamode_cursor_bg);
	if (alt_key_indicator == NULL){
		PRINT(stderr, "Couldn't render alt_key_indicator surface: %s\n", TTF_GetError());
		return TERM_FAILURE;
	}

	str[0] = 'C';
	ctrl_key_indicator = TTF_RenderUNICODE_Shaded(font, str, metamode_cursor_fg, metamode_cursor_bg);
	if (ctrl_key_indicator == NULL){
		PRINT(stderr, "Couldn't render ctrl_key_indicator surface: %s\n", TTF_GetError());
		return TERM_FAILURE;
	}

	str[0] = 0x2191;
	shift_key_indicator = TTF_RenderUNICODE_Shaded(font, str, metamode_cursor_fg, metamode_cursor_bg);
	if (shift_key_indicator == NULL){
		PRINT(stderr, "Couldn't render shift_key_indicator surface: %s\n", TTF_GetError());
		return TERM_FAILURE;
	}

	str[0] = 'a';
	altsym_indicator = TTF_RenderUNICODE_Shaded(font, str, metamode_cursor_fg, metamode_cursor_bg);
	if (shift_key_indicator == NULL){
		PRINT(stderr, "Couldn't render altsym_indicator surface: %s\n", TTF_GetError());
		return TERM_FAILURE;
	}

	str[0] = 'M';
	metamode_cursor = TTF_RenderUNICODE_Shaded(font, str, metamode_cursor_fg, metamode_cursor_bg);
	if (metamode_cursor == NULL){
		PRINT(stderr, "Couldn't render metamode_cursor surface: %s\n", TTF_GetError());
		return TERM_FAILURE;
	}

	/* Get the size of the font */
	int minx, maxx, miny, maxy;
	if(TTF_GlyphMetrics(font, (Uint16)'X', &minx, &maxx, &miny, &maxy, &advance) != 0){
		PRINT(stderr, "Could not get Glyph Metrics: %s\n", TTF_GetError());
		return TERM_FAILURE;
	}

	text_width = advance;
	text_height = maxy - miny;
	text_height_padding = TTF_FontLineSkip(font) - text_height;
	text_height += text_height_padding;
	PRINT(stderr, "Character h: %d w:%d (h padding: %d) advance: %d\n", text_height, text_width, text_height_padding, advance);

	return TERM_SUCCESS;
}

void font_uninit(){

	glyph_cache_clear();
	SDL_FreeSurface(metamode_cursor);
	SDL_FreeSurface(ctrl_key_indicator);
	SDL_FreeSurface(alt_key_indicator);
	SDL_FreeSurface(shift_key_indicator);
	if(font != NULL){
		TTF_CloseFont(font);
	}
}

void handle_activeevent(int gain, int state){
	if (gain && prefs->auto_show_vkb){
		PRINT(stderr, "Got ActiveEvent - initializing keyboard\n");
		virtualkeyboard_show();
	}
}

void handle_mousedown(Uint16 x, Uint16 y){
	/* check for hits in the metamode_hitbox */
	if((x >= prefs->metamode_hitbox->x) &&
	   (x <= prefs->metamode_hitbox->x + prefs->metamode_hitbox->w) &&
	   (y >= prefs->metamode_hitbox->y) &&
	   (y <= prefs->metamode_hitbox->y + prefs->metamode_hitbox->h)) {
		/* hit in the box */
		metamode_toggle();
	}
	/* touching the screen will reveal the keyboard on a Passport,
	 * since the system wide gesture doesn't work to reveal. */
	if (prefs->auto_show_vkb){
		virtualkeyboard_show();
	}

	/* check for symmenu touches */
	if(current_symmenu != NULL){
		send_metamode_keystrokes(symkey_for_mousedown(current_symmenu, x, y));
	}
}

void handle_virtualkeyboard_event(bps_event_t *event){
	PRINT(stderr, "Virtual Keyboard event\n");
	int event_code = bps_event_get_code(event);
	int vkb_h;
	int resolution[2] = {screen->w, screen->h};

	vkb_h = get_virtualkeyboard_height();

	switch (event_code){
	case VIRTUALKEYBOARD_EVENT_VISIBLE:
		setup_screen_size(resolution[0], resolution[1] - vkb_h);
		virtualkeyboard_visible = 1;
		break;
	case VIRTUALKEYBOARD_EVENT_HIDDEN:
		setup_screen_size(resolution[0], resolution[1]);
		virtualkeyboard_visible = 0;
		break;
	case VIRTUALKEYBOARD_EVENT_INFO:
		vkb_h = virtualkeyboard_visible ? virtualkeyboard_event_get_height(event) : 0;
		setup_screen_size(resolution[0], resolution[1] - vkb_h);
		break;
	default:
		fprintf(stderr, "Unknown keyboard event code %d\n", event_code);
		break;
	}
}

void rescreen(int w, int h){

	int width  = w == -1 ? screen->w : w;
	int height = h == -1 ? screen->h : h;
	int vkb_h = 0;
	screen = SDL_SetVideoMode(width, height, PB_D_PIXELS, SDL_HWSURFACE | SDL_DOUBLEBUF);
	/* reset the font size as well */
	font_uninit();
	if(font_init(prefs->font_size) == TERM_FAILURE){
		fprintf(stderr, "Couldn't initialize font\n");
		exit_application = 1;
	}

	setup_screen_size(width, height);
	if(virtualkeyboard_visible){
		vkb_h = get_virtualkeyboard_height();
		setup_screen_size(width, height - vkb_h);
	}
	mark_screen_dirty(1);
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

static int dispatch_action(const t49_action_t *action) {
	if (action == NULL) {
		return 0;
	}

	switch (action->kind) {
	case T49_ACTION_SEND_BYTES:
		return send_metamode_keystrokes(action->as.bytes.data);
	case T49_ACTION_SEND_TERMINFO:
		return send_metamode_keystrokes(action->as.terminfo_name);
	case T49_ACTION_BUILTIN:
		switch (action->as.builtin.id) {
		case T49_BUILTIN_ALT_DOWN:
			toggle_vkeymod(KEYMOD_ALT);
			return 1;
		case T49_BUILTIN_CTRL_DOWN:
			toggle_vkeymod(KEYMOD_CTRL);
			return 1;
		case T49_BUILTIN_RESCREEN:
			rescreen(-1, -1);
			return 1;
		case T49_BUILTIN_PASTE_CLIPBOARD:
			io_paste_from_clipboard();
			return 1;
		default:
			return 0;
		}
	}

	return 0;
}

static int dispatch_action_string(const char *value) {
	t49_action_t action;
	if (!action_parse(value, &action)) {
		return 0;
	}
	return dispatch_action(&action);
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

void handleKeyboardEvent(screen_event_t screen_event)
{
	int screen_val, screen_flags, screen_alt_val;
	int modifiers;
	int num_chars;
	int vkbd_h;
	int metamode_just_set = 0;
	UChar c[CHARACTER_BUFFER];
	UChar *target = c;
	struct timespec now;
	uint64_t now_t, diff_t, metamode_last_t;
	const char* keys = NULL;
	int32_t last_len = 0;
	int32_t bs_i = 0;
	size_t upcase_len = 0;
	UChar backspace = 0x8;

	screen_get_event_property_iv(screen_event, SCREEN_PROPERTY_KEY_FLAGS, &screen_flags);
	screen_get_event_property_iv(screen_event, SCREEN_PROPERTY_KEY_SYM, &screen_val);
	screen_get_event_property_iv(screen_event, SCREEN_PROPERTY_KEY_ALTERNATE_SYM, &screen_alt_val);
	screen_get_event_property_iv(screen_event, SCREEN_PROPERTY_KEY_MODIFIERS, &modifiers);
	//screen_get_event_property_iv(screen_event, SCREEN_PROPERTY_KEY_CAP, &cap);

	if (screen_flags & KEY_DOWN) {
		PRINT(stderr, "The '%d' key was pressed (modifiers: %d) (char %c) (alt %d)\n", (int)screen_val, modifiers, (char)screen_val, (int)screen_alt_val);
		fflush(stdout);

		/* if we're toggling metamode on or off with doubletap */
		if((screen_val == metamode_doubletap_key) && !(screen_flags & KEY_REPEAT)){
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
		if(screen_val == KEYCODE_BB_SYM_KEY){
			if(!(screen_flags & KEY_REPEAT)){
				symmenu_toggle(prefs->main_symmenu);
			} else{
				/* they are holding it down */
				symmenu_stick();
			}
			return;
		}

		if(screen_val == KEYCODE_BB_ALT_KEY){
			if (prefs->sticky_alt_key) {
				if(screen_flags & KEY_REPEAT){
					return;
				} else {
					altsym_toggle();
					return;
				}
			}
		}
		
		if(!virtualkeyboard_visible
		   && ((screen_val == KEYCODE_LEFT_SHIFT) || (screen_val == KEYCODE_RIGHT_SHIFT))){
			if (prefs->sticky_shift_key) {
				if(screen_flags & KEY_REPEAT){
					return;
				} else {
					toggle_vkeymod(KEYMOD_SHIFT);
					return;
				}
			}
		}

		/* metamode sticky keys don't trigger repreat */
		if (metamode && !metamode_just_set) {
			keys = keystroke_lookup((char)screen_val, prefs->metamode_sticky_keys);
			if (keys != NULL){
				dispatch_action_string(keys);
				return;
			}
		}

		/* handle key repeat to upcase / metamode */
		if ((screen_flags & KEY_REPEAT) &&
		    prefs->keyhold_actions &&
		    !is_int_member(prefs->keyhold_actions_exempt, screen_val)) {
			if (!key_repeat_done) {
				/* Check for a metamode toggle key first */
				if (screen_val == prefs->metamode_hold_key) {
					io_write_master(&backspace, 1);
					metamode_toggle();
					key_repeat_done = 1;
					return;
				}
				
				symmenu_t *menu = get_keyhold_actions(screen_val);
				if (menu == NULL) {
					return;
				}
				
				last_len = io_upcase_last_write(&target, CHARACTER_BUFFER);
				/* write backspace */
				for(bs_i = 1; bs_i <= last_len; ++bs_i) {
					io_write_master(&backspace, 1);
				}

				/* select the mapping */

				// uppercase automatically if there's no accents or accents disabled
				if ((menu->entries[1].to == NULL) || (!prefs->keyhold_accents)) {
					/* We can upcase, send last_len backspaces and then the upcase char.
					 * Note that this really only works if the program on the other
					 * end of the line understands unicode, and can marry up backspaces
					 * with codepoints, instead of just blindly deleting one byte at a time. */
					dispatch_action_string(menu->entries[0].to);
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
			keys = keystroke_lookup((char)screen_val, prefs->metamode_keys);
			if(keys != NULL){
				dispatch_action_string(keys);
				metamode_toggle();
				return;
			}
			// else
			keys = keystroke_lookup((char)screen_val, prefs->metamode_func_keys);
			if(keys != NULL){
				dispatch_action_string(keys);
			}
			metamode_toggle();
			return;
		}

		/* handle alt keys */
		if (altsym_lock) {
			keys = keystroke_lookup((char)screen_val, prefs->altsym_entries);
			altsym_toggle();
			if (keys != NULL){
				dispatch_action_string(keys);
				return;
			}
		}

		/* handle sym keys */
		if (current_symmenu != NULL) {
			keys = keystroke_lookup((char)screen_val, current_symmenu->entries);
			if (keys != NULL){
				dispatch_action_string(keys);
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
		switch (screen_val) {
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
			PRINT(stderr, "Modifier %d\n", screen_val);
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
			num_chars = terminal_key_sequence(screen_val, modifiers, c);
			int nc;
			for(nc = 0; nc < num_chars; ++nc){
				PRINT(stderr, "Writing 0x%x\n", (int)c[nc]);
			}
			io_write_master((const UChar*)&c, num_chars);
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
	ws.ws_xpixel = screen ? screen->w : 0;
	ws.ws_ypixel = screen ? screen->h : 0;

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
	if(SDL_LockMutex(input_mutex) == -1){
		fprintf(stderr, "Couldn't lock input mutex - exiting\n");
		exit_application = 1;
	}
}
void unlock_input(){
	if(SDL_UnlockMutex(input_mutex) == -1){
		fprintf(stderr, "Couldn't unlock input mutex - exiting\n");
		exit_application = 1;
	}
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
			setup_screen_size(screen->w, screen->h);
			/* and force the number of columns */
			cols = ncols;
			set_tty_window_size();
			ghostty_bridge_resize((uint16_t)cols, (uint16_t)rows,
			                      (uint32_t)advance, (uint32_t)text_height);
			mark_screen_dirty(1);
		}
	}
}

static int sdl_init() {
	/* init the input mutex */
	input_mutex = SDL_CreateMutex();
	
	/* init the event input pipe */
	if(pipe(event_pipe) == -1){
		fprintf(stderr, "Couldn't create event pipe\n");
		return TERM_FAILURE;
	}

	/* Initialize SDL */
	if (SDL_Init(SDL_INIT_VIDEO) < 0 ) {
		PRINT(stderr, "Couldn't initialize SDL: %s\n",SDL_GetError());
		return TERM_FAILURE;
	}
	PRINT(stderr, "Post SDL_Init()\n");

	SDL_EventState(SDL_SYSWMEVENT, SDL_ENABLE);
	
	// We get keyboard events from the SysWMEvents
	SDL_EventState(SDL_KEYDOWN, SDL_IGNORE);
	SDL_EventState(SDL_KEYUP, SDL_IGNORE);

	screen_window_t window;
	screen_context_t context;

	SDL_SysWMinfo info;
	if(get_wm_info(&info) != 1){
		fprintf(stderr, "Couldn't get WM Info: %s\n",SDL_GetError());
		return TERM_FAILURE;
	}

	/* grab the orientation and resolution so we can start up that way */
	window = info.mainWindow;
	context = info.context;
	int wm_size[2] = {0,0};

	if (screen_get_window_property_iv(window, SCREEN_PROPERTY_SIZE, wm_size)) {
		fprintf(stderr, "Cannot get resolution: %s", strerror(errno));
		return TERM_FAILURE;
	}
	PRINT(stderr, "wm size returned: w:%d, h:%d\n", wm_size[0], wm_size[1]);

	/* Initialize the TTF library */
	if ( TTF_Init() < 0 ) {
		PRINT(stderr, "Couldn't initialize TTF: %s\n",SDL_GetError());
		SDL_Quit();
		return TERM_FAILURE;
	}

	/* set screen idle mode */
	if(!prefs->screen_idle_awake){
		setenv("SCREEN_IDLE_NORMAL", "1", 0);
	}

	/* check to verify if the wm returned the native resolution */
	if (getenv("WIDTH") != NULL && getenv("HEIGHT") != NULL) {
		if(wm_size[0] != atoi(getenv("WIDTH")) || wm_size[1] != atoi(getenv("HEIGHT"))){
			fprintf(stderr, "SDL_WMInfo returned non-native screen resolution - forcing\n");
			wm_size[0] = atoi(getenv("WIDTH"));
			wm_size[1] = atoi(getenv("HEIGHT"));
		}
	}

	screen = SDL_SetVideoMode(wm_size[0], wm_size[1], PB_D_PIXELS, SDL_HWSURFACE | SDL_DOUBLEBUF);
	if ( screen == NULL ) {
		PRINT(stderr, "Couldn't set %d x %d x %d video mode: %s\n", wm_size[0], wm_size[1], PB_D_PIXELS, SDL_GetError());
		TTF_Quit();
		SDL_Quit();
		return TERM_FAILURE;
	}

	if(font_init(prefs->font_size) == TERM_FAILURE){
		PRINT(stderr, "Couldn't initialize font\n");
		TTF_Quit();
		SDL_Quit();
		return TERM_FAILURE;
	}

	/* Don't show the mouse icon */
	SDL_ShowCursor(SDL_DISABLE);

	/* initialize the number of rows and columns */
	rows = screen->h / text_height;
	cols = screen->w / text_width;


	setup_screen_size(screen->w, screen->h);
	
	/* and set the last 'press' */
	clock_gettime(CLOCK_MONOTONIC, &metamode_last);

	if(ghostty_bridge_init((uint16_t)cols, (uint16_t)rows, 1000) != 0){
		fprintf(stderr, "Unable to initialize libghostty-vt terminal\n");
		return TERM_FAILURE;
	}
	ghostty_bridge_resize((uint16_t)cols, (uint16_t)rows,
	                      (uint32_t)advance, (uint32_t)text_height);

	return TERM_SUCCESS;
}

void app_shutdown(void){

	SDL_DestroyMutex(input_mutex);

	font_uninit();
	SDL_FreeSurface(screen);

	ghostty_bridge_uninit();

	TTF_Quit();
	SDL_Quit();

	destroy_preferences(prefs);

	io_uninit();
}

static SDL_Color ghostty_sdl_color(ghostty_bridge_rgb_t rgb) {
	SDL_Color out;
	out.r = rgb.r;
	out.g = rgb.g;
	out.b = rgb.b;
	out.unused = 0;
	return out;
}

#define GLYPH_CACHE_SIZE 2048

typedef struct glyph_cache_entry {
	uint32_t codepoint;
	uint32_t fg;
	uint32_t bg;
	int style;
	SDL_Surface *surface;
} glyph_cache_entry_t;

static glyph_cache_entry_t glyph_cache[GLYPH_CACHE_SIZE];

static uint32_t color_key(SDL_Color c) {
	return ((uint32_t)c.r << 16) | ((uint32_t)c.g << 8) | (uint32_t)c.b;
}

static unsigned glyph_cache_hash(uint32_t codepoint, int style, uint32_t fg, uint32_t bg) {
	uint32_t h = codepoint * 2654435761u;
	h ^= fg * 2246822519u;
	h ^= bg * 3266489917u;
	h ^= (uint32_t)style * 668265263u;
	return (unsigned)(h & (GLYPH_CACHE_SIZE - 1));
}

static void glyph_cache_clear(void) {
	int i;
	for (i = 0; i < GLYPH_CACHE_SIZE; ++i) {
		if (glyph_cache[i].surface != NULL) {
			SDL_FreeSurface(glyph_cache[i].surface);
			glyph_cache[i].surface = NULL;
		}
	}
}

static SDL_Surface *glyph_cache_lookup(uint32_t codepoint, int style,
                                       SDL_Color fg, SDL_Color bg) {
	uint32_t fg_key = color_key(fg);
	uint32_t bg_key = color_key(bg);
	unsigned idx = glyph_cache_hash(codepoint, style, fg_key, bg_key);
	glyph_cache_entry_t *entry = &glyph_cache[idx];
	UChar str[3] = {0, 0, 0};

	if (entry->surface != NULL && entry->codepoint == codepoint &&
	    entry->style == style && entry->fg == fg_key && entry->bg == bg_key) {
		return entry->surface;
	}

	if (entry->surface != NULL) {
		SDL_FreeSurface(entry->surface);
		entry->surface = NULL;
	}

	str[0] = (codepoint <= 0xffff) ? (UChar)codepoint : (UChar)0xfffd;
	TTF_SetFontStyle(font, style);
	entry->surface = TTF_RenderUNICODE_Shaded(font, str, fg, bg);
	if (entry->surface == NULL) {
		return NULL;
	}
	entry->codepoint = codepoint;
	entry->style = style;
	entry->fg = fg_key;
	entry->bg = bg_key;
	return entry->surface;
}

struct ghostty_render_context {
	ghostty_bridge_frame_t frame;
	int failed;
};

static void render_ghostty_cell(uint16_t x, uint16_t y,
                                const ghostty_bridge_cell_t *cell,
                                void *userdata) {
	struct ghostty_render_context *ctx = (struct ghostty_render_context *)userdata;
	SDL_Color fg;
	SDL_Color bg;
	SDL_Surface *glyph = NULL;
	SDL_Rect destrect;
	int style = TTF_STYLE_NORMAL;

	if (ctx == NULL || cell == NULL || x >= cols || y >= rows) { return; }

	fg = ghostty_sdl_color(cell->has_fg ? cell->fg : ctx->frame.default_fg);
	bg = ghostty_sdl_color(cell->has_bg ? cell->bg : ctx->frame.default_bg);

	if (cell->inverse || flash ||
	    (draw_cursor && ctx->frame.cursor_visible &&
	     x == ctx->frame.cursor_x && y == ctx->frame.cursor_y)) {
		SDL_Color tmp = fg;
		fg = bg;
		bg = tmp;
	}

	destrect.x = x * advance;
	destrect.y = y * text_height;
	destrect.w = advance;
	destrect.h = text_height;
	SDL_FillRect(screen, &destrect, SDL_MapRGB(screen->format, bg.r, bg.g, bg.b));

	if (!cell->has_text || cell->codepoint == 0 || cell->wide_tail || cell->invisible) {
		return;
	}

	if (cell->bold) { style |= TTF_STYLE_BOLD; }
	if (cell->italic) { style |= TTF_STYLE_ITALIC; }
	if (cell->underline) { style |= TTF_STYLE_UNDERLINE; }

	glyph = glyph_cache_lookup(cell->codepoint, style, fg, bg);
	if (glyph == NULL) {
		PRINT(stderr, "Ghostty glyph render failed for U+%04x: %s\n",
		      (unsigned)cell->codepoint, TTF_GetError());
		ctx->failed = 1;
		return;
	}
	if (SDL_BlitSurface(glyph, NULL, screen, &destrect) != 0) {
		PRINT(stderr, "Ghostty glyph blit failed: %s\n", SDL_GetError());
		ctx->failed = 1;
	}
}

static int render_ghostty(int force_full_repaint) {
	static int prev_cursor_valid = 0;
	static int prev_cursor_visible = 0;
	static uint16_t prev_cursor_x = 0;
	static uint16_t prev_cursor_y = 0;
	struct ghostty_render_context ctx;
	SDL_Color bg;
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

	force_full_repaint = force_full_repaint || flash ||
		ctx.frame.dirty == GHOSTTY_BRIDGE_DIRTY_FULL;

	bg = ghostty_sdl_color(ctx.frame.default_bg);
	if (force_full_repaint) {
		SDL_FillRect(screen, NULL, SDL_MapRGB(screen->format, bg.r, bg.g, bg.b));
	}
	if (force_full_repaint || ctx.frame.dirty != 0) {
		if (ghostty_bridge_visit_cells(!force_full_repaint, render_ghostty_cell, &ctx) != 0 || ctx.failed) {
			fprintf(stderr, "ghostty render: visit_cells failed\n");
			return 0;
		}
	}
	if (!force_full_repaint && cursor_changed) {
		if (prev_cursor_visible && prev_cursor_y < rows &&
		    ghostty_bridge_visit_row(prev_cursor_y, render_ghostty_cell, &ctx) != 0) {
			fprintf(stderr, "ghostty render: visit old cursor row failed\n");
			return 0;
		}
		if (cursor_visible && ctx.frame.cursor_y < rows &&
		    (!prev_cursor_visible || prev_cursor_y != ctx.frame.cursor_y) &&
		    ghostty_bridge_visit_row(ctx.frame.cursor_y, render_ghostty_cell, &ctx) != 0) {
			fprintf(stderr, "ghostty render: visit new cursor row failed\n");
			return 0;
		}
	}
	prev_cursor_valid = 1;
	prev_cursor_visible = cursor_visible;
	prev_cursor_x = ctx.frame.cursor_x;
	prev_cursor_y = ctx.frame.cursor_y;

	TTF_SetFontStyle(font, TTF_STYLE_NORMAL);

	if(metamode && metamode_cursor != NULL){
		SDL_Rect destrect;
		destrect.x = (cols-1) * advance;
		destrect.y = 0;
		destrect.w = metamode_cursor->w;
		destrect.h = metamode_cursor->h;
		SDL_BlitSurface(metamode_cursor, NULL, screen, &destrect);
	}

	if(vmodifiers & KEYMOD_CTRL){
		SDL_Rect destrect;
		destrect.x = (cols-1) * advance;
		destrect.y = 1 * text_height;
		destrect.w = ctrl_key_indicator->w;
		destrect.h = ctrl_key_indicator->h;
		SDL_BlitSurface(ctrl_key_indicator, NULL, screen, &destrect);
	}

	if(vmodifiers & KEYMOD_ALT){
		SDL_Rect destrect;
		destrect.x = (cols-1) * advance;
		destrect.y = 2 * text_height;
		destrect.w = alt_key_indicator->w;
		destrect.h = alt_key_indicator->h;
		SDL_BlitSurface(alt_key_indicator, NULL, screen, &destrect);
	}

	if(vmodifiers & KEYMOD_SHIFT){
		SDL_Rect destrect;
		destrect.x = (cols-1) * advance;
		destrect.y = 3 * text_height;
		destrect.w = shift_key_indicator->w;
		destrect.h = shift_key_indicator->h;
		SDL_BlitSurface(shift_key_indicator, NULL, screen, &destrect);
	}

	if (altsym_lock) {
		SDL_Rect destrect;
		destrect.x = (cols-1) * advance;
		destrect.y = 3 * text_height;
		destrect.w = shift_key_indicator->w;
		destrect.h = shift_key_indicator->h;
		SDL_BlitSurface(altsym_indicator, NULL, screen, &destrect);
	}

	if ((current_symmenu != NULL) && (current_symmenu->surface != NULL)) {
		SDL_Rect destrect;
		destrect.w = current_symmenu->surface->w;
		destrect.h = current_symmenu->surface->h;
		destrect.x = 0;
		destrect.y = screen->h - current_symmenu->surface->h;;
		if (SDL_BlitSurface(current_symmenu->surface, NULL, screen, &destrect) != 0) {
			PRINT(stderr, "Symmenu blit failed: %s\n", SDL_GetError());
			return 1;
		}
	}

	ghostty_bridge_finish_frame();
	SDL_Flip(screen);

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

extern int SDL_PrivateQuit(void);
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
		SDL_PrivateQuit();
	} else {
		PRINT(stderr, "Got SIGCHILD for process other than %d\n", child_pid);
	}
	errno = old_errno;
}

/* This function is run in an SDL_Thread, and will check
 * for either input event indication or data from the
 * shell, then run the render loop
 */
int run_render(void* data){

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
		 * poke wakes us for every SDL event, but inert system events
		 * (SYSWMEVENT/ACTIVEEVENT/unknown) leave screen_dirty clear, so
		 * we skip the full-screen FillRect + page-flip that was causing
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
	/* never reached */
	return 0;
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
	
	prefs = read_preferences(PREFS_FILE_PATH);
	if (is_passport()) {
		prefs->auto_show_vkb = 1;
	}

	/* set auto orientation */
	setenv("AUTO_ORIENTATION", "1", 0);

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

	/* initialize SDL video etc */
	if (TERM_SUCCESS != sdl_init()) {
		PRINT(stderr, "Unable to initialize SDL\n");
		app_shutdown();
		return TERM_FAILURE;
	}

	/* render the symmenus */
	prefs->main_symmenu->surface = render_symmenu(screen, prefs, prefs->main_symmenu);
	for (char c = 'a'; c <= 'z'; ++c) {
		size_t idx = (size_t)(c - 'a');

		// lowercase
		symmenu_t *m = prefs->accent_menus[idx][0];
		if (m->entries[1].to != NULL) {
			m->surface = render_symmenu(screen, prefs, m);
		}

		// uppercase
		m = prefs->accent_menus[idx][1];
		if (m->entries[1].to != NULL) {
			m->surface = render_symmenu(screen, prefs, m);
		}
	}

	if (prefs->auto_show_vkb) {
		virtualkeyboard_show();
	}

	/* start up main event loop */
	SDL_Thread *render_thread = SDL_CreateThread(run_render, NULL);
	/* screen_dirty starts set for the first frame, but the render thread
	 * blocks in select() until either pty output or an SDL event arrives.
	 * Poke it once so launch never sits on an undrawn black backbuffer. */
	indicate_event_input();
	while (!exit_application) {

		//Request and process all available events
		SDL_Event event;

		SDL_WaitEvent(&event);
		lock_input();
		switch (event.type) {
		case SDL_QUIT:
			exit_application = 1;
			break;
		case SDL_VIDEORESIZE:
			rescreen(event.resize.w, event.resize.h);
			mark_screen_dirty(1);
			break;
		case SDL_KEYDOWN:
			{
				PRINT(stderr, "SDL_KEYDOWN\n");
				UChar uc;
				char sdlkey = event.key.keysym.sym;
				uc = (UChar)sdlkey;
				io_write_master(&uc, 1);
			}
			mark_screen_dirty(0);
			break;
		case SDL_SYSWMEVENT:
			{
				bps_event_t* bps_event = event.syswm.msg->event;
				int screene_type;
				int domain = bps_event_get_domain(bps_event);
				PRINT(stderr, "Unhandled SYSWMEVENT: %d\n", domain);
			}
			break;
		case SDL_MOUSEBUTTONDOWN:
			handle_mousedown(event.button.x, event.button.y);
			mark_screen_dirty(1);
			break;
		case SDL_ACTIVEEVENT:
			handle_activeevent(event.active.gain, event.active.state);
			break;
		default:
			PRINT(stderr, "Unknown Event: %d\n", event.type);
			break;
		}
		indicate_event_input();
		unlock_input();
	}

	PRINT(stderr, "Exiting run loop\n");
	SDL_KillThread(render_thread);
	virtualkeyboard_hide();
	app_shutdown();

	return 0;
}
