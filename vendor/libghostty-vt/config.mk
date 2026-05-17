# Tunables for the libghostty-vt BB10/QNX integration. One place for paths/flags.

# Parent flake providing the BBNDK FHS shell (`#shell` sources
# bbndk-env_10_3_1_995.sh then exec "$@": qcc/nto*/blackberry-*/bb-* on PATH).
# By default, walk up from this directory and use the BlackBerry staging
# flake (identified by flake.nix plus a BBNDK checkout); override
# PARENT_FLAKE or BB_SHELL for other layouts.
PARENT_FLAKE ?= $(shell d='$(CURDIR)'; while [ "$$d" != / ]; do \
	if [ -f "$$d/flake.nix" ] && { [ -d "$$d/bbndk-linux" ] || [ -d "$$d/bbndk-win32" ]; }; then printf '%s\n' "$$d"; exit 0; fi; \
	d=$$(dirname "$$d"); \
	done)
BB_SHELL     ?= nix run $(PARENT_FLAKE)\#shell --

# Zig comes from `make deps` (nix build .#deps -> build/deps/zig/bin/zig).
# Resolved lazily so `make help` works before deps exist.
ZIG ?= $(shell echo "$(CURDIR)/build/deps/zig/bin/zig")

# Zig codegen target. Zig has NO QNX target — this is a generic ARM-EABI
# triple used purely as a code generator; the .a is pure computation linked
# by qcc.
ZIG_TARGET ?= arm-freestanding-eabi
ZIG_MCPU   ?= cortex_a9
ZIG_OPT    ?= ReleaseSmall

# BBNDK cross toolchain (resolved inside BB_SHELL). Match Term49's exact
# compiler — Term49/Makefile uses `qcc -V4.6.3,gcc_ntoarmv7le` (GCC 4.6.3),
# NOT fen-blackberry's bare `-Vgcc_ntoarmv7le`. ABI must match Term49's.
CC := qcc -V4.6.3,gcc_ntoarmv7le

