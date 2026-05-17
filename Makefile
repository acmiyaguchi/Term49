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
LIBS     += -lconfig -lSDL12 -lTouchControlOverlay

# Term49 uses libghostty-vt as its terminal parser/state model and renderer
# source of truth. Build the freestanding Ghostty static library on demand.
GHOSTTY_DIR   := vendor/libghostty-vt
GHOSTTY_BUILD := $(GHOSTTY_DIR)/build/ghostty
GHOSTTY_A     := $(GHOSTTY_BUILD)/lib/libghostty-vt.a
GHOSTTY_H     := $(GHOSTTY_BUILD)/include/ghostty/vt.h
INCLUDE += -I$(GHOSTTY_BUILD)/include
LIBS += $(GHOSTTY_A) -lm -lgcc

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

.PHONY: all clean libghostty-vt package-dev package-release deploy sign

all: $(BINARY_PATH)

libghostty-vt: $(GHOSTTY_A) $(GHOSTTY_H)

$(GHOSTTY_A) $(GHOSTTY_H):
	$(MAKE) -C $(GHOSTTY_DIR) deps lib

$(BINARY): $(BINARY_PATH)

$(BINARY_PATH): $(GHOSTTY_A) $(GHOSTTY_H) $(OBJS)
	mkdir -p $(ASSET)
	$(CC) $(CFLAGS) $(LIBPATHS) $(LDOPTS) $(OBJS) $(LIBS) -o $(BINARY_PATH)

%.o: %.c
	$(CC) $(CFLAGS) -c $(DEFINES) $< -o $@

clean:
	@rm -fv src/*.o
	@rm -fv $(BINARY_PATH)
	@rmdir -v $(ASSET) 2>/dev/null || true
	@rm -fv $(BINARY).bar

package-dev: $(BINARY_PATH)
	blackberry-nativepackager -devMode -package $(BINARY).bar bar-descriptor.xml -configuration Device-Debug

deploy: package-dev
	bb-deploy $(BINARY).bar

package-release: $(BINARY_PATH)
	blackberry-nativepackager -package $(BINARY).bar bar-descriptor.xml

sign: package-release
	blackberry-signer -bbidtoken ./signing/$(BBIDTOKEN) -storepass $(KEYSTOREPASS) -keystore ./signing/$(KEYSTORE) $(BINARY).bar
