#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/keycodes.h>

#include "bitmap.h"
#include "ghostty_bridge.h"
#include "io.h"
#include "platform.h"
#include "renderer.h"
#include "term_types.h"
#include "url_pick.h"

#include "qrcodegen.h"

#define URL_PICK_MAX_MATCHES 64
#define URL_PICK_MAX_URL_LEN 512
#define URL_PICK_MAX_COLS    256
#define URL_PICK_MAX_ROWS    96

/* QR_MAX_VERSION 10 = 57 modules per side, plenty for URLs up to a few
 * hundred bytes at low ECC. The temp + qrcode buffers are sized to that
 * cap; the encoder picks the smallest fitting version up to this limit. */
#define QR_MAX_VERSION 10

static const rgb_t HINT_FG       = { 0,   0,   0   };
static const rgb_t HINT_BG       = { 255, 220, 0   };  /* high-contrast yellow */
static const rgb_t HINT_SEL_BG   = { 0,   200, 60  };  /* picked-URL marker */
static const rgb_t PROMPT_FG     = { 0,   0,   0   };
static const rgb_t PROMPT_BG     = { 255, 220, 0   };
static const rgb_t QR_LIGHT      = { 255, 255, 255 };
static const rgb_t QR_DARK       = { 0,   0,   0   };

typedef struct url_match {
	char     url[URL_PICK_MAX_URL_LEN];
	uint16_t row;
	uint16_t col;
	char     label[3];     /* up to 2 letters + NUL */
	int      label_len;
} url_match_t;

typedef enum mode {
	MODE_INACTIVE = 0,
	MODE_HINT,
	MODE_ACTION,
	MODE_QR,
} pick_mode_t;

static pick_mode_t      g_mode = MODE_INACTIVE;
static url_match_t g_matches[URL_PICK_MAX_MATCHES];
static int         g_n_matches = 0;
static int         g_selected = -1;
static char        g_typed_prefix[2] = { 0, 0 };
static int         g_typed_len = 0;

static uint8_t   g_qr_qrcode[qrcodegen_BUFFER_LEN_FOR_VERSION(QR_MAX_VERSION)];
static uint8_t   g_qr_tmp[qrcodegen_BUFFER_LEN_FOR_VERSION(QR_MAX_VERSION)];
static int       g_qr_encoded = 0;
static bitmap_t *g_qr_bitmap = NULL;
static int       g_qr_side_px = 0;

/* === URL scanning ======================================================== */

typedef struct row_scan_ctx {
	char ascii[URL_PICK_MAX_COLS];
	char has_content[URL_PICK_MAX_COLS];
	uint16_t cols;
} row_scan_ctx_t;

static void row_visitor(uint16_t x, uint16_t y,
                        const ghostty_bridge_cell_t *cell, void *userdata){
	(void)y;
	row_scan_ctx_t *ctx = (row_scan_ctx_t *)userdata;
	if (x >= ctx->cols || x >= URL_PICK_MAX_COLS) {
		return;
	}
	if (cell->wide_tail || !cell->has_text) {
		ctx->ascii[x] = ' ';
		ctx->has_content[x] = 0;
		return;
	}
	uint32_t cp = cell->codepoint;
	/* URLs are ASCII; map any non-ASCII / non-printable cell to a space
	 * so a state-machine matcher never crosses through it. */
	if (cp >= 0x21 && cp < 0x7F) {
		ctx->ascii[x] = (char)cp;
		ctx->has_content[x] = 1;
	} else {
		ctx->ascii[x] = ' ';
		ctx->has_content[x] = 0;
	}
}

static int is_url_char(unsigned char c){
	if (c <= 0x20 || c >= 0x7F) return 0;
	if (c >= '0' && c <= '9') return 1;
	if (c >= 'A' && c <= 'Z') return 1;
	if (c >= 'a' && c <= 'z') return 1;
	switch (c) {
	case '-': case '.': case '_': case '~':
	case ':': case '/': case '?': case '#':
	case '[': case ']': case '@':
	case '!': case '$': case '&': case '\'':
	case '(': case ')':
	case '*': case '+': case ',': case ';':
	case '=': case '%':
		return 1;
	}
	return 0;
}

/* Trim trailing prose punctuation. ) ] } only come off when the URL has
 * no matching opener -- "(see https://x.y)" should strip ')' but
 * "/wiki/Foo_(bar)" should keep it. Same logic for ] and }. */
static int trim_trailing(const char *s, int len){
	while (len > 0) {
		char c = s[len - 1];
		switch (c) {
		case '.': case ',': case ';': case ':':
		case '!': case '?':
		case '>': case '\'': case '"':
			len--;
			continue;
		case ')': case ']': case '}': {
			char open = (c == ')') ? '(' : (c == ']') ? '[' : '{';
			int has_open = 0;
			for (int j = 0; j < len - 1; j++) {
				if (s[j] == open) { has_open = 1; break; }
			}
			if (has_open) {
				return len;   /* balanced: keep the closer */
			}
			len--;
			continue;
		}
		}
		break;
	}
	return len;
}

static int label_from_index(int i, int total, char out[3]){
	if (total <= 26) {
		out[0] = (char)('a' + i);
		out[1] = '\0';
		out[2] = '\0';
		return 1;
	}
	out[0] = (char)('a' + (i / 26));
	out[1] = (char)('a' + (i % 26));
	out[2] = '\0';
	return 2;
}

static int has_prefix_at(const char *buf, int n, int i, const char *prefix, int prefix_len){
	if (i + prefix_len > n) return 0;
	if (memcmp(buf + i, prefix, (size_t)prefix_len) != 0) return 0;
	/* Reject mid-token matches like "xhttp://" or "ahttps://". */
	if (i > 0 && is_url_char((unsigned char)buf[i - 1])) return 0;
	return 1;
}

static int scan_buffer(const char *buf, const uint16_t *row_at,
                       const uint16_t *col_at, int n){
	int i = 0;
	int added = 0;

	while (i < n && g_n_matches < URL_PICK_MAX_MATCHES) {
		int start = -1;
		int prefix_len = 0;
		int prepend_https = 0;

		if (has_prefix_at(buf, n, i, "https://", 8)) {
			start = i; prefix_len = 8;
		} else if (has_prefix_at(buf, n, i, "http://", 7)) {
			start = i; prefix_len = 7;
		} else if (has_prefix_at(buf, n, i, "www.", 4)) {
			start = i; prefix_len = 4;
			prepend_https = 1;
		} else {
			i++;
			continue;
		}

		i = start + prefix_len;
		/* Require a host character right after the scheme. Without this
		 * "http:///path" passes the matched > prefix_len check below. */
		if (i >= n) {
			continue;
		}
		{
			unsigned char hc = (unsigned char)buf[i];
			int host_ok = (hc >= 'a' && hc <= 'z') ||
			              (hc >= 'A' && hc <= 'Z') ||
			              (hc >= '0' && hc <= '9') ||
			              hc == '_' || hc == '-';
			if (!host_ok) {
				continue;
			}
		}
		while (i < n && is_url_char((unsigned char)buf[i])) {
			i++;
		}
		int end = i;
		int matched = end - start;
		matched = trim_trailing(buf + start, matched);
		if (matched <= prefix_len) {
			continue;  /* no host body after the scheme */
		}

		/* Reject overlong URLs rather than silently truncating: a
		 * truncated URL that opens/copies/QRs is a correctness bug. */
		int prepend_len = prepend_https ? (int)(sizeof("https://") - 1) : 0;
		if (prepend_len + matched + 1 > (int)sizeof(g_matches[0].url)) {
			continue;
		}

		url_match_t *m = &g_matches[g_n_matches];
		m->row = row_at[start];
		m->col = col_at[start];

		int dst = 0;
		if (prepend_https) {
			memcpy(m->url, "https://", (size_t)prepend_len);
			dst = prepend_len;
		}
		memcpy(m->url + dst, buf + start, (size_t)matched);
		m->url[dst + matched] = '\0';

		g_n_matches++;
		added++;
	}
	return added;
}

/* === public API ========================================================== */

int url_pick_active(void){
	return g_mode != MODE_INACTIVE;
}

void url_pick_exit(void){
	g_mode = MODE_INACTIVE;
	g_n_matches = 0;
	g_selected = -1;
	g_typed_len = 0;
	g_typed_prefix[0] = g_typed_prefix[1] = '\0';
	g_qr_encoded = 0;
	bitmap_free(g_qr_bitmap);
	g_qr_bitmap = NULL;
	g_qr_side_px = 0;
}

int url_pick_enter(ghostty_bridge_t *bridge, const url_pick_layout_t *layout){
	if (bridge == NULL || layout == NULL ||
	    layout->cols == 0 || layout->rows == 0) {
		return 0;
	}

	url_pick_exit();   /* clear any leftover state */

	/* Flat buffer covering every visible row. Adjacent rows are joined
	 * when the previous row's last column has content (the wrap case);
	 * otherwise we drop a space between them so unrelated lines cannot
	 * fuse a URL together. */
	static char     buf[URL_PICK_MAX_COLS * URL_PICK_MAX_ROWS + URL_PICK_MAX_ROWS];
	static uint16_t row_at[sizeof(buf) / sizeof(char)];
	static uint16_t col_at[sizeof(buf) / sizeof(char)];

	uint16_t cols = layout->cols;
	uint16_t rows = layout->rows;
	if (cols > URL_PICK_MAX_COLS) cols = URL_PICK_MAX_COLS;
	if (rows > URL_PICK_MAX_ROWS) rows = URL_PICK_MAX_ROWS;

	int n = 0;
	row_scan_ctx_t ctx;
	ctx.cols = cols;

	for (uint16_t y = 0; y < rows; y++) {
		for (int k = 0; k < cols; k++) {
			ctx.ascii[k] = ' ';
			ctx.has_content[k] = 0;
		}
		if (ghostty_bridge_visit_row(bridge, y, row_visitor, &ctx) != 0) {
			continue;
		}
		int last_content_col = -1;
		for (int x = 0; x < cols; x++) {
			if (n >= (int)sizeof(buf) - 1) break;
			buf[n] = ctx.ascii[x];
			row_at[n] = y;
			col_at[n] = (uint16_t)x;
			n++;
			if (ctx.has_content[x]) {
				last_content_col = x;
			}
		}
		/* Separator only when the row clearly didn't wrap. */
		if (last_content_col < cols - 1 && n < (int)sizeof(buf) - 1) {
			buf[n] = ' ';
			row_at[n] = y;
			col_at[n] = (uint16_t)cols;
			n++;
		}
	}

	scan_buffer(buf, row_at, col_at, n);

	if (g_n_matches == 0) {
		url_pick_exit();
		return 0;
	}

	for (int i = 0; i < g_n_matches; i++) {
		g_matches[i].label_len =
			label_from_index(i, g_n_matches, g_matches[i].label);
	}
	g_mode = MODE_HINT;
	return 1;
}

static char sym_to_letter(int sym){
	if (sym >= 'a' && sym <= 'z') return (char)sym;
	if (sym >= 'A' && sym <= 'Z') return (char)(sym - 'A' + 'a');
	return 0;
}

static int handle_hint_key(char letter){
	int needed = g_n_matches > 26 ? 2 : 1;
	if (g_typed_len >= needed) {
		g_typed_len = 0;
	}
	g_typed_prefix[g_typed_len++] = letter;
	if (g_typed_len < needed) {
		return 1;
	}

	int picked = -1;
	for (int i = 0; i < g_n_matches; i++) {
		if (g_matches[i].label_len == needed &&
		    memcmp(g_matches[i].label, g_typed_prefix, (size_t)needed) == 0) {
			picked = i;
			break;
		}
	}
	g_typed_len = 0;
	if (picked < 0) {
		return 1;   /* unknown label: stay in hint mode */
	}
	g_selected = picked;
	g_mode = MODE_ACTION;
	return 1;
}

/* Encode `url` into g_qr_qrcode. The bitmap is rasterized lazily on the
 * first render once layout is known. Returns 1 on success. */
static int build_qr(const char *url){
	g_qr_encoded = qrcodegen_encodeText(url, g_qr_tmp, g_qr_qrcode,
	                                    qrcodegen_Ecc_LOW, 1, QR_MAX_VERSION,
	                                    qrcodegen_Mask_AUTO, true) ? 1 : 0;
	bitmap_free(g_qr_bitmap);
	g_qr_bitmap = NULL;
	g_qr_side_px = 0;
	return g_qr_encoded;
}

static int handle_action_key(char letter, platform_t *platform){
	if (g_selected < 0 || g_selected >= g_n_matches) {
		url_pick_exit();
		return 1;
	}
	const char *url = g_matches[g_selected].url;
	switch (letter) {
	case 'o':
		platform_open_url(platform, url);
		url_pick_exit();
		return 1;
	case 'c':
		if (io_copy_to_clipboard(url, strlen(url)) == 0) {
			platform_toast(platform, "URL copied to clipboard");
		} else {
			platform_toast(platform, "Clipboard copy failed");
		}
		url_pick_exit();
		return 1;
	case 'q':
		if (!build_qr(url)) {
			platform_toast(platform, "QR encode failed (URL too long?)");
			url_pick_exit();
			return 1;
		}
		g_mode = MODE_QR;
		return 1;
	}
	return 1;   /* swallow everything else in action mode */
}

int url_pick_handle_key(int sym, int modifiers, int repeat, platform_t *platform){
	(void)modifiers;
	if (g_mode == MODE_INACTIVE) {
		return 0;
	}
	/* Repeats are swallowed but never advance state. Otherwise a held
	 * hint letter would auto-fire its action via MODE_HINT -> MODE_ACTION
	 * -> 'o' open, and a key held into QR view would dismiss it on the
	 * second tick. */
	if (repeat) {
		return 1;
	}
	if (sym == KEYCODE_ESCAPE) {
		url_pick_exit();
		return 1;
	}
	if (g_mode == MODE_QR) {
		url_pick_exit();
		return 1;
	}
	char letter = sym_to_letter(sym);
	if (letter == 0) {
		return 1;
	}
	if (g_mode == MODE_HINT)   return handle_hint_key(letter);
	if (g_mode == MODE_ACTION) return handle_action_key(letter, platform);
	return 1;
}

/* === rendering =========================================================== */

static void draw_text(renderer_t *r, int x, int y, const char *s,
                      rgb_t fg, rgb_t bg, int advance){
	for (int i = 0; s[i] != '\0'; i++) {
		renderer_draw_glyph(r, x + i * advance, y,
		                    (uint32_t)(unsigned char)s[i],
		                    FONT_STYLE_NORMAL, fg, bg);
	}
}

static void draw_status_line(renderer_t *r, const url_pick_layout_t *layout,
                             const char *text){
	int y = layout->fb_h - layout->text_height;
	if (y < 0) y = 0;
	rect_t row = { 0, y, layout->fb_w, layout->text_height };
	renderer_fill_rect(r, &row, PROMPT_BG);
	draw_text(r, 0, y, text, PROMPT_FG, PROMPT_BG, layout->advance);
}

static void draw_hint_label(renderer_t *r, const url_pick_layout_t *layout,
                            const url_match_t *m, int label_cells,
                            rgb_t fg, rgb_t bg){
	int px = m->col * layout->advance;
	int py = layout->grid_top_pad + m->row * layout->text_height;
	rect_t bgrect = { px, py, label_cells * layout->advance, layout->text_height };
	renderer_fill_rect(r, &bgrect, bg);
	for (int k = 0; k < m->label_len; k++) {
		renderer_draw_glyph(r, px + k * layout->advance, py,
		                    (uint32_t)(unsigned char)m->label[k],
		                    FONT_STYLE_NORMAL, fg, bg);
	}
}

/* Build the cached QR bitmap once. ~hundreds of module fills land in
 * the bitmap on entry, then every subsequent frame just blits one
 * memcpy via renderer_draw_bitmap -- avoids re-issuing fill_rect per
 * module on the per-frame paint hot path. */
static void rasterize_qr(const url_pick_layout_t *layout){
	if (!g_qr_encoded) return;
	int qsize = qrcodegen_getSize(g_qr_qrcode);
	if (qsize <= 0) return;

	int quiet = 4;
	int total = qsize + 2 * quiet;
	int max_px = (layout->fb_w < layout->fb_h ? layout->fb_w : layout->fb_h)
	             - 2 * layout->text_height;
	if (max_px < total) max_px = total;
	int module_px = max_px / total;
	if (module_px < 2) module_px = 2;
	int side_px = total * module_px;

	bitmap_t *b = bitmap_alloc(side_px, side_px, BITMAP_FMT_RGBA8888);
	if (b == NULL) return;
	bitmap_fill_rect(b, NULL, QR_LIGHT);
	for (int my = 0; my < qsize; my++) {
		for (int mx = 0; mx < qsize; mx++) {
			if (!qrcodegen_getModule(g_qr_qrcode, mx, my)) continue;
			rect_t mrect = {
				(quiet + mx) * module_px,
				(quiet + my) * module_px,
				module_px, module_px,
			};
			bitmap_fill_rect(b, &mrect, QR_DARK);
		}
	}
	g_qr_bitmap  = b;
	g_qr_side_px = side_px;
}

static void draw_qr(renderer_t *r, const url_pick_layout_t *layout){
	if (g_qr_bitmap == NULL) {
		rasterize_qr(layout);
		if (g_qr_bitmap == NULL) return;
	}
	int ox = (layout->fb_w - g_qr_side_px) / 2;
	int oy = (layout->fb_h - g_qr_side_px) / 2;
	if (ox < 0) ox = 0;
	if (oy < 0) oy = 0;
	renderer_draw_bitmap(r, ox, oy, g_qr_bitmap);
	draw_status_line(r, layout, " QR: scan with phone   any key dismisses");
}

void url_pick_render(renderer_t *r, const url_pick_layout_t *layout){
	if (r == NULL || layout == NULL || g_mode == MODE_INACTIVE) {
		return;
	}

	if (g_mode == MODE_HINT) {
		int label_cells = (g_n_matches > 26 ? 2 : 1);
		for (int i = 0; i < g_n_matches; i++) {
			draw_hint_label(r, layout, &g_matches[i], label_cells, HINT_FG, HINT_BG);
		}
		char status[96];
		if (g_typed_len > 0) {
			snprintf(status, sizeof(status),
			         " URL: %d matches  typed=%.*s  esc=cancel",
			         g_n_matches, g_typed_len, g_typed_prefix);
		} else {
			snprintf(status, sizeof(status),
			         " URL: %d matches  type %s  esc=cancel",
			         g_n_matches, label_cells == 2 ? "2 letters" : "letter");
		}
		draw_status_line(r, layout, status);
		return;
	}

	if (g_mode == MODE_ACTION) {
		if (g_selected >= 0 && g_selected < g_n_matches) {
			int label_cells = (g_n_matches > 26 ? 2 : 1);
			draw_hint_label(r, layout, &g_matches[g_selected], label_cells,
			                HINT_FG, HINT_SEL_BG);
		}
		char status[160];
		const char *url = (g_selected >= 0 && g_selected < g_n_matches)
			? g_matches[g_selected].url : "";
		snprintf(status, sizeof(status),
		         " %.80s   o=open  c=copy  q=qr  esc=cancel", url);
		draw_status_line(r, layout, status);
		return;
	}

	if (g_mode == MODE_QR) {
		draw_qr(r, layout);
		return;
	}
}
