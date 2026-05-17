# libghostty-vt BB10/QNX build support — cross-build + on-device smoke check.
#
# Nix only materializes a pinned Zig (+ Ghostty source) via `make deps`; qcc
# compile/link run outside Nix inside the parent BBNDK FHS shell.

include config.mk

.PHONY: help deps lib harness smoke all clean

help:
	@echo 'libghostty-vt targets:'
	@echo '  deps    — nix build .#deps -> build/deps (pinned zig + ghostty src)'
	@echo '  lib     — zig build libghostty-vt.a + headers [BBNDK FHS]'
	@echo '  harness — qcc-link smoke_terminal example     [BBNDK FHS]'
	@echo '  smoke   — push+run terminal smoke on Q10 (SMOKE_OK)'
	@echo '  all     — deps->lib->harness->smoke'
	@echo '  clean   — rm -rf build/'

# Explicit `path:` — this tree lives inside the Term49 repo, so a bare
# `.#deps` would resolve to the enclosing repo. `path:$(CURDIR)` treats this
# dir as the flake root.
deps:
	nix build "path:$(CURDIR)#deps" --out-link build/deps

lib harness:
	$(BB_SHELL) bash scripts/cross-build.sh $@

smoke:
	bash scripts/smoke-device.sh

all: deps lib harness smoke
	@echo "libghostty-vt checks complete — see Q10 stdout above for SMOKE_OK"

clean:
	rm -rf build
