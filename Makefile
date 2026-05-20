CC := qcc

INCLUDE := -I$(QNX_TARGET)/usr/include
INCLUDE += -I$(QNX_TARGET)/usr/include/freetype2
VENDOR_PREBUILT := ./vendor/prebuilt-bb10

INCLUDE += -I$(VENDOR_PREBUILT)/include

# BB10 libraries
LIBPATHS	:= -L$(QNX_TARGET)/armle-v7/lib
LIBS    	:= -lbps -licui18n -licuuc -lscreen -lm -lfreetype -lclipboard

# Defines
DEFINES := -D_FORTIFY_SOURCE=2 -D__PLAYBOOK__ -fstack-protector-strong

# OpenGL libraries
LIBPATHS += -L$(QNX_TARGET)/armle-v7/usr/lib

# Include vendored BB10 prebuilt libs
LIBPATHS += -L$(VENDOR_PREBUILT)/lib
LIBS     += -lSDL12 -lTouchControlOverlay

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

ASSET      	:= Device-Debug
BINARY     	:= Term49
BINARY_PATH	:= $(ASSET)/$(BINARY)

SRCS := $(wildcard src/*.c)
OBJS := $(SRCS:.c=.o )

include ./signing/bbpass

# deploy/connect call the BlackBerry NDK tools directly (like qcc and
# blackberry-nativepackager above). Credentials come from signing/bbpass;
# refuse to run while BBPASS is still its "" placeholder.
check-creds = test -n '$(strip $(filter-out "",$(BBPASS)))' || { echo 'Set BBPASS in signing/bbpass before deploying' >&2; exit 1; }

.PHONY: all clean libghostty-vt package-dev package-release deploy connect sign

all: $(BINARY_PATH)

libghostty-vt: $(GHOSTTY_A) $(GHOSTTY_H)

$(GHOSTTY_A) $(GHOSTTY_H):
	$(MAKE) -C $(GHOSTTY_DIR) deps lib

$(BINARY): $(BINARY_PATH)

$(BINARY_PATH): $(GHOSTTY_A) $(GHOSTTY_H) $(LUA_A) $(OBJS)
	mkdir -p $(ASSET)
	$(CC) $(CFLAGS) $(LIBPATHS) $(LDOPTS) $(OBJS) $(LIBS) -o $(BINARY_PATH)

# Lua sources are upstream third-party C: build them without Term49's
# app DEFINES (no __PLAYBOOK__/_FORTIFY_SOURCE), only LUA_USE_POSIX for QNX.
# More specific stem than the generic %.o rule, so make prefers this for
# vendor/lua/*.c.
$(LUA_DIR)/%.o: $(LUA_DIR)/%.c
	$(CC) $(CFLAGS) -DLUA_USE_POSIX -c $< -o $@

$(LUA_A): $(LUA_OBJS)
	mkdir -p $(dir $@)
	$(LUA_AR) rcs $@ $(LUA_OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c $(DEFINES) $< -o $@

clean:
	@rm -fv src/*.o
	@rm -rfv $(LUA_DIR)/build $(LUA_OBJS)
	@rm -fv $(BINARY_PATH)
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
