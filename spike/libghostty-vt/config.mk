# Tunables for the libghostty-vt feasibility spike. One place for paths/flags.
# Mirrors fen-blackberry/config.mk. See ../../../docs/term49-modernization.md #36
# and the plan in ~/.claude/plans/okay-in-the-worktree-eager-whisper.md.

# Parent flake providing the BBNDK FHS shell (`#shell` sources
# bbndk-env_10_3_1_995.sh then exec "$@": qcc/nto*/blackberry-*/bb-* on PATH).
PARENT_FLAKE := /mnt/data/fun/blackberry
BB_SHELL     := nix run $(PARENT_FLAKE)\#shell --

# Zig comes from `make deps` (nix build .#deps -> build/deps/zig/bin/zig).
# Resolved lazily so `make help` works before deps exist.
ZIG ?= $(shell echo "$(CURDIR)/build/deps/zig/bin/zig")

# Zig codegen target. Zig has NO QNX target — this is a generic ARM-EABI
# triple used purely as a code generator; the .a is pure computation linked
# by qcc. Step D (abi-probe) may overwrite build/abi/zig_target; cross-build.sh
# prefers that file over this default.
ZIG_TARGET ?= arm-freestanding-eabi
ZIG_MCPU   ?= cortex_a9
ZIG_OPT    ?= ReleaseSmall

# BBNDK cross toolchain (resolved inside BB_SHELL). Match Term49's exact
# compiler — Term49/Makefile uses `qcc -V4.6.3,gcc_ntoarmv7le` (GCC 4.6.3),
# NOT fen-blackberry's bare `-Vgcc_ntoarmv7le`. ABI must match Term49's.
CC := qcc -V4.6.3,gcc_ntoarmv7le

# Device deploy (overridable via env / parent .env). USB-net dev-mode default.
BB_DEVICE  ?= 169.254.0.1
DEPLOY_DIR ?= /accounts/1000/shared/documents
