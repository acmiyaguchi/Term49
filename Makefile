CC := qcc

INCLUDE := -I$(QNX_TARGET)/usr/include
INCLUDE += -I$(QNX_TARGET)/usr/include/freetype2

# BB10 libraries
LIBPATHS	:= -L$(QNX_TARGET)/armle-v7/lib
LIBS    	:= -lbps -licui18n -licuuc -lscreen -lm -lfreetype -lclipboard -lsocket

# Defines
DEFINES := -D_FORTIFY_SOURCE=2 -D__PLAYBOOK__ -fstack-protector-strong

LIBPATHS += -L$(QNX_TARGET)/armle-v7/usr/lib

# Term49 uses libghostty-vt as its terminal parser/state model and renderer
# source of truth. Build the freestanding Ghostty static library on demand.
GHOSTTY_DIR   := vendor/libghostty-vt
GHOSTTY_BUILD := $(GHOSTTY_DIR)/build/ghostty
GHOSTTY_A     := $(GHOSTTY_BUILD)/lib/libghostty-vt.a
GHOSTTY_H     := $(GHOSTTY_BUILD)/include/ghostty/vt.h
INCLUDE += -I$(GHOSTTY_BUILD)/include
LIBS += $(GHOSTTY_A)

# Term49 embeds Lua 5.4 (vendored, statically linked) as its config language
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

ASSET      	:= Device-Debug
BINARY     	:= Term49
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

include ./signing/bbpass

# deploy/connect call the BlackBerry NDK tools directly (like qcc and
# blackberry-nativepackager above). Credentials come from signing/bbpass;
# refuse to run while BBPASS is still its "" placeholder.
check-creds = test -n '$(strip $(filter-out "",$(BBPASS)))' || { echo 'Set BBPASS in signing/bbpass before deploying' >&2; exit 1; }

.PHONY: all clean libghostty-vt package-dev package-release deploy connect sign

all: $(BINARY_PATH) $(CTL_PATH)

libghostty-vt: $(GHOSTTY_A) $(GHOSTTY_H)

$(GHOSTTY_A) $(GHOSTTY_H):
	$(MAKE) -C $(GHOSTTY_DIR) deps lib

$(BINARY): $(BINARY_PATH)

$(BINARY_PATH): $(GHOSTTY_A) $(GHOSTTY_H) $(LUA_A) $(OBJS)
	mkdir -p $(ASSET)
	$(CC) $(CFLAGS) $(LIBPATHS) $(LDOPTS) $(OBJS) $(LIBS) -o $(BINARY_PATH)

# Client objects: no app DEFINES, but -Isrc so they find control_proto.h.
$(CTL_DIR)/%.ctl.o: $(CTL_DIR)/%.c
	$(CC) $(CFLAGS) $(DEPFLAGS) -Isrc -c $< -o $@
src/%.ctl.o: src/%.c
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

$(CTL_PATH): $(CTL_OBJS)
	mkdir -p $(ASSET)
	$(CC) $(CFLAGS) $(LIBPATHS) $(LDOPTS) $(CTL_OBJS) $(CTL_LIBS) -o $(CTL_PATH)

# Lua sources are upstream third-party C: build them without Term49's
# app DEFINES (no __PLAYBOOK__/_FORTIFY_SOURCE), only LUA_USE_POSIX for QNX.
# More specific stem than the generic %.o rule, so make prefers this for
# vendor/lua/*.c.
$(LUA_DIR)/%.o: $(LUA_DIR)/%.c
	$(CC) $(CFLAGS) $(DEPFLAGS) -DLUA_USE_POSIX -c $< -o $@

$(LUA_A): $(LUA_OBJS)
	mkdir -p $(dir $@)
	$(LUA_AR) rcs $@ $(LUA_OBJS)

%.o: %.c
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $(DEFINES) $< -o $@

# Auto-generated header dependencies (absent on a clean tree -> the leading
# - makes the include a no-op until the first build writes them).
DEPS := $(OBJS:.o=.d) $(CTL_OBJS:.o=.d) $(LUA_OBJS:.o=.d)
-include $(DEPS)

clean:
	@rm -fv src/*.o src/*.ctl.o $(CTL_DIR)/*.ctl.o
	@rm -fv $(DEPS)
	@rm -rfv $(LUA_DIR)/build $(LUA_OBJS)
	@rm -fv $(BINARY_PATH) $(CTL_PATH)
	@rmdir -v $(ASSET) 2>/dev/null || true
	@rm -fv $(BINARY).bar

package-dev: $(BINARY_PATH)
	blackberry-nativepackager -devMode -package $(BINARY).bar bar-descriptor.xml -configuration Device-Debug

deploy: package-dev
	@$(check-creds)
	@blackberry-deploy -installApp -launchApp -device $(BBIP) -password $(BBPASS) -package $(BINARY).bar

connect:
	@$(check-creds)
	@blackberry-connect $(BBIP) -password $(BBPASS)

package-release: $(BINARY_PATH)
	blackberry-nativepackager -package $(BINARY).bar bar-descriptor.xml

sign: package-release
	blackberry-signer -bbidtoken ./signing/$(BBIDTOKEN) -storepass $(KEYSTOREPASS) -keystore ./signing/$(KEYSTORE) $(BINARY).bar
