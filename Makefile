CC := qcc

INCLUDE := -I$(QNX_TARGET)/usr/include
INCLUDE += -I$(QNX_TARGET)/usr/include/freetype2

# BB10 libraries
LIBPATHS	:= -L$(QNX_TARGET)/armle-v7/lib
LIBS    	:= -lbps -licui18n -licuuc -lscreen -lm -lfreetype -lclipboard -lsocket

# Defines
DEFINES := -D_FORTIFY_SOURCE=2 -D__PLAYBOOK__ -fstack-protector-strong

LIBPATHS += -L$(QNX_TARGET)/armle-v7/usr/lib

# Term50 uses libghostty-vt as its terminal parser/state model and renderer
# source of truth. Build the freestanding Ghostty static library on demand.
GHOSTTY_DIR   := vendor/libghostty-vt
GHOSTTY_BUILD := $(GHOSTTY_DIR)/build/ghostty
GHOSTTY_A     := $(GHOSTTY_BUILD)/lib/libghostty-vt.a
GHOSTTY_H     := $(GHOSTTY_BUILD)/include/ghostty/vt.h
INCLUDE += -I$(GHOSTTY_BUILD)/include
LIBS += $(GHOSTTY_A)

# Vendored Nayuki QR-Code-generator (MIT). Compiled WITHOUT Term50's app
# DEFINES so the third-party source builds as upstream intended; the URL
# picker pulls it in via src/url_pick.c.
QR_DIR  := vendor/qrcodegen
QR_SRC  := $(QR_DIR)/qrcodegen.c
QR_OBJS := $(QR_SRC:.c=.o)
INCLUDE += -I$(QR_DIR)

# Term50 embeds Lua 5.4 (vendored, statically linked) as its config language
# and scripting runtime. Static archive => no new .so / bar-descriptor asset.
# Sources live at the lua/lua repo root; exclude the interpreter main (lua.c),
# the amalgamation (onelua.c), and the internal test harness (ltests.c).
LUA_DIR  := vendor/lua
LUA_SRC  := $(filter-out $(LUA_DIR)/lua.c $(LUA_DIR)/onelua.c $(LUA_DIR)/ltests.c,$(wildcard $(LUA_DIR)/*.c))
LUA_OBJS := $(LUA_SRC:.c=.o)
LUA_A    := $(LUA_DIR)/build/liblua.a
LUA_AR   ?= ntoarmv7-ar
INCLUDE  += -I$(LUA_DIR)
LIBS     += $(LUA_A) -lm -lgcc

# Optimized by default for on-device latency. Use `make DEBUGFLAGS='-O0 -g -DDEBUGMSGS'`
# when you specifically need a chatty debug build.
DEBUGFLAGS	?= -O2
CFLAGS    	:= $(INCLUDE) -V4.6.3,gcc_ntoarmv7le -Wc,-std=gnu99 $(DEBUGFLAGS)
LDOPTS    	:= -Wl,-z,relro -Wl,-z,now

# Header-dependency tracking: emit a .d beside each object so a changed header
# (e.g. a struct in types.h) rebuilds every dependent object. Routed through the
# preprocessor with -Wp, because qcc does not forward driver-level -MMD. Without
# this, a stale .o keeps an old struct layout and links into a silent ABI
# mismatch that only surfaces as a runtime crash. -MP adds phony header targets
# so deleting a header doesn't wedge the build.
DEPFLAGS  	= -Wp,-MMD,$(@:.o=.d) -Wp,-MT,$@ -Wp,-MP

# Output configuration. Overridable so `package-release` can rebuild the same
# objects into Device-Release without editing this file (see that target).
ASSET      	?= Device-Debug
BINARY     	:= Term50
BINARY_PATH	:= $(ASSET)/$(BINARY)

SRCS := $(wildcard src/*.c)
OBJS := $(SRCS:.c=.o )

# termctl: a standalone control-socket client (#5). It links NONE of the app
# libs -- only libsocket and the shared framing parser src/control_proto.c.
# Its objects use a distinct .ctl.o suffix so they stay out of the server's
# OBJS and never collide with the generic %.o rule (which applies app DEFINES).
CTL_DIR  := tools/termctl
CTL_BIN  := termctl
CTL_PATH := $(ASSET)/$(CTL_BIN)
CTL_SRCS := $(CTL_DIR)/main.c src/control_proto.c
CTL_OBJS := $(CTL_SRCS:.c=.ctl.o)
CTL_LIBS := -lsocket -lm

# Device credentials (BBIP, BBPASS) for deploy/connect live in an untracked
# .env (see .env.example). Soft include so non-deploy targets work without it.
-include .env

# deploy/connect call the BlackBerry NDK tools directly (like qcc and
# blackberry-nativepackager above). Credentials come from .env; refuse to run
# while BBPASS is still its "" placeholder.
check-creds = test -n '$(strip $(filter-out "",$(BBPASS)))' || { echo 'Set BBPASS in .env before deploying' >&2; exit 1; }

.PHONY: all clean icons libghostty-vt package-dev package-release deploy connect \
        bbnix-bundle stage-bbnix clean-bbnix fonts-bundle stage-fonts clean-fonts \
        fen-bundle stage-fen clean-fen

all: $(BINARY_PATH) $(CTL_PATH)

libghostty-vt: $(GHOSTTY_A) $(GHOSTTY_H)

$(GHOSTTY_A) $(GHOSTTY_H):
	$(MAKE) -C $(GHOSTTY_DIR) deps lib

$(BINARY): $(BINARY_PATH)

$(BINARY_PATH): $(GHOSTTY_A) $(GHOSTTY_H) $(LUA_A) $(OBJS) $(QR_OBJS)
	mkdir -p $(ASSET)
	$(CC) $(CFLAGS) $(LIBPATHS) $(LDOPTS) $(OBJS) $(QR_OBJS) $(LIBS) -o $(BINARY_PATH)

# Client objects: no app DEFINES, but -Isrc so they find control_proto.h.
$(CTL_DIR)/%.ctl.o: $(CTL_DIR)/%.c
	$(CC) $(CFLAGS) $(DEPFLAGS) -Isrc -c $< -o $@
src/%.ctl.o: src/%.c
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

$(CTL_PATH): $(CTL_OBJS)
	mkdir -p $(ASSET)
	$(CC) $(CFLAGS) $(LIBPATHS) $(LDOPTS) $(CTL_OBJS) $(CTL_LIBS) -o $(CTL_PATH)

# Lua sources are upstream third-party C: build them without Term50's
# app DEFINES (no __PLAYBOOK__/_FORTIFY_SOURCE), only LUA_USE_POSIX for QNX.
# More specific stem than the generic %.o rule, so make prefers this for
# vendor/lua/*.c.
$(LUA_DIR)/%.o: $(LUA_DIR)/%.c
	$(CC) $(CFLAGS) $(DEPFLAGS) -DLUA_USE_POSIX -c $< -o $@

# Same exemption for the vendored QR encoder. Pure C, no app defines.
$(QR_DIR)/%.o: $(QR_DIR)/%.c
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

$(LUA_A): $(LUA_OBJS)
	mkdir -p $(dir $@)
	$(LUA_AR) rcs $@ $(LUA_OBJS)

%.o: %.c
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $(DEFINES) $< -o $@

# Auto-generated header dependencies (absent on a clean tree -> the leading
# - makes the include a no-op until the first build writes them).
DEPS := $(OBJS:.o=.d) $(CTL_OBJS:.o=.d) $(LUA_OBJS:.o=.d) $(QR_OBJS:.o=.d)
-include $(DEPS)

clean:
	@rm -fv src/*.o src/*.ctl.o $(CTL_DIR)/*.ctl.o
	@rm -fv $(DEPS)
	@rm -rfv $(LUA_DIR)/build $(LUA_OBJS)
	@rm -fv $(QR_OBJS)
	@rm -rfv Device-Debug Device-Release
	@rm -rfv share/fen share/root result-fonts
	@rm -fv $(BINARY).bar

icons:
	tools/generate-icons.sh

package-dev: icons
	$(MAKE) stage-bbnix
	$(MAKE) stage-fonts
	$(MAKE) stage-fen
	$(MAKE) ASSET=Device-Debug all
	blackberry-nativepackager -devMode -package $(BINARY).bar bar-descriptor.xml -configuration Device-Debug

deploy: package-dev
	@$(check-creds)
	@blackberry-deploy -installApp -launchApp -device $(BBIP) -password $(BBPASS) -package $(BINARY).bar

connect:
	@$(check-creds)
	@blackberry-connect $(BBIP) -password $(BBPASS)

# Distributable, release-mode bar. Builds the binaries into Device-Release
# (a recursive make so the shared objects relink to the release output dir),
# then packages WITHOUT -devMode so the manifest has
# Application-Development-Mode: false. BlackBerry's signing servers are gone, so
# this unsigned bar is sideloaded (Sachesi/DBL) rather than signed.
package-release: icons
	$(MAKE) stage-bbnix
	$(MAKE) stage-fonts
	$(MAKE) stage-fen
	$(MAKE) ASSET=Device-Release all
	blackberry-nativepackager -package $(BINARY).bar bar-descriptor.xml -configuration Device-Release

# --- Required bbnix userland bundle ---------------------------------------
# bbnix is a hard dependency: it supplies the login shell (zsh), the terminfo
# DB, ssh/tmux/mosh and a CA bundle. `make stage-bbnix` builds the pinned bbnix
# flake (.#bbnix-bundle) and stages it under share/bbnix, which
# bar-descriptor.xml packages to app/native/bbnix. package-dev/package-release
# run stage-bbnix automatically, so they REQUIRE Nix and a BB10 sysroot: bbnix
# builds are impure (they read $BBNIX_SYSROOT and need a relaxed sandbox).
#
# Override the variant with BBNIX_BUNDLE=bbnix-bundle-ssh (or -minimal); the
# default bbnix-bundle alias is the full variant.
BBNIX_BUNDLE ?= bbnix-bundle

bbnix-bundle:
	@test -n "$(BBNIX_SYSROOT)" || { echo 'Set BBNIX_SYSROOT to your bbndk-linux tree, e.g. BBNIX_SYSROOT=/mnt/data/fun/bbdev/sdk/bbndk-linux' >&2; exit 1; }
	BBNIX_SYSROOT="$(BBNIX_SYSROOT)" nix build --impure --option sandbox relaxed .#$(BBNIX_BUNDLE) -o result-bbnix

stage-bbnix: bbnix-bundle
	rm -rf share/bbnix
	mkdir -p share/bbnix
	cp -RL result-bbnix/. share/bbnix/
	chmod -R u+w share/bbnix
	touch share/bbnix/.keep

# --- Optional bundled terminal fonts ----------------------------------------
# Fonts are sourced from pinned nixpkgs packages and staged into share/fonts,
# which bar-descriptor.xml packages to app/native/fonts. Keep the generated
# payload out of git; only share/fonts/.keep is tracked.
TERM50_FONTS_BUNDLE ?= term50-fonts-bundle

fonts-bundle:
	nix build .#$(TERM50_FONTS_BUNDLE) -o result-fonts

stage-fonts: fonts-bundle
	rm -rf share/fonts
	mkdir -p share/fonts
	cp -RL result-fonts/. share/fonts/
	chmod -R u+w share/fonts
	touch share/fonts/.keep

clean-bbnix:
	rm -rf share/bbnix result-bbnix
	mkdir -p share/bbnix && touch share/bbnix/.keep

clean-fonts:
	rm -rf share/fonts result-fonts
	mkdir -p share/fonts && touch share/fonts/.keep

# --- Bundled fen coding agent -----------------------------------------------
# fen-blackberry builds the Fen CLI for BB10/QNX with bbnix's GCC toolchain and
# static curl/OpenSSL/zlib. Stage the real executable outside PATH and install a
# tiny PATH wrapper: Fen's appended Lua payload is found via argv[0], so the
# wrapper execs the binary by absolute on-device path.
FEN_DIR     ?= vendor/fen-blackberry
FEN_BINARY  := $(FEN_DIR)/build/fen
FEN_STAGE   := share/fen/bin/fen
FEN_WRAPPER := share/root/bin/fen

fen-bundle:
	@test -n "$(BBNIX_SYSROOT)" || { echo 'Set BBNIX_SYSROOT to your bbndk-linux tree, e.g. BBNIX_SYSROOT=/mnt/data/fun/bbdev/sdk/bbndk-linux' >&2; exit 1; }
	$(MAKE) -C $(FEN_DIR) BBNIX_SYSROOT="$(BBNIX_SYSROOT)" fen

stage-fen: fen-bundle
	rm -rf share/fen share/root/bin/fen
	mkdir -p share/fen/bin share/root/bin
	install -m755 $(FEN_BINARY) $(FEN_STAGE)
	printf '%s\n' '#!/bin/sh' \
	  'if [ -n "$$SANDBOX" ]; then' \
	  '  exec "$$SANDBOX/app/native/fen/bin/fen" "$$@"' \
	  'fi' \
	  'case "$$0" in' \
	  '  /*) native=$${0%/root/bin/fen}; exec "$$native/fen/bin/fen" "$$@" ;;' \
	  'esac' \
	  'echo "fen: cannot locate bundled executable; SANDBOX is not set" >&2' \
	  'exit 127' > $(FEN_WRAPPER)
	chmod 755 $(FEN_WRAPPER)

clean-fen:
	rm -rf share/fen share/root
	$(MAKE) -C $(FEN_DIR) clean
