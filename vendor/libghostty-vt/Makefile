# libghostty-vt BB10/QNX build support.
#
# Nix only materializes a pinned Zig (+ Ghostty source) via `make deps`; qcc
# compile/link run outside Nix inside the parent BBNDK FHS shell.

include config.mk

.PHONY: help deps lib harness all clean

help:
	@echo 'libghostty-vt targets:'
	@echo '  deps    — nix build .#deps -> build/deps (pinned zig + ghostty src)'
	@echo '  lib     — zig build libghostty-vt.a + headers [BBNDK FHS]'
	@echo '  harness — qcc-link smoke_terminal example     [BBNDK FHS]'
	@echo '  all     — deps->lib->harness'
	@echo '  clean   — rm -rf build/'

# Explicit `path:` — this tree lives inside the Term50 repo, so a bare
# `.#deps` would resolve to the enclosing repo. `path:$(CURDIR)` treats this
# dir as the flake root.
deps:
	nix build "path:$(CURDIR)#deps" --out-link build/deps

lib harness:
	$(BB_SHELL) bash scripts/cross-build.sh $@

all: deps lib harness
	@echo "libghostty-vt build and link check complete"

clean:
	rm -rf build
