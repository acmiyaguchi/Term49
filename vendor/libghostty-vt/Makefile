# libghostty-vt BB10/QNX build support — cross-build + on-device checks.
#
# Nix only materializes a pinned Zig (+ Ghostty source) via `make deps`; qcc
# compile/link + readelf run outside Nix inside the parent BBNDK FHS shell.
#
# Check map:
#   abi-probe = determine qcc ARM float ABI for Zig target selection
#   abi-zig+smoke-abi = prove Zig object <-> qcc C ABI
#   lib       = build libghostty-vt.a and headers
#   harness+smoke = standalone terminal API example on the Q10

include config.mk

.PHONY: help deps abi-probe abi-zig smoke-abi lib harness probe smoke smoke-probe all clean

help:
	@echo 'libghostty-vt targets:'
	@echo '  deps        — nix build .#deps -> build/deps (pinned zig + ghostty src)'
	@echo '  abi-probe   — qcc readelf -A -> pick Zig triple       [BBNDK FHS]'
	@echo '  abi-zig     — zig lib + qcc-link ABI probe binary     [BBNDK FHS]'
	@echo '  smoke-abi   — push+run ABI probe on Q10 (ABI_OK)'
	@echo '  lib         — zig build libghostty-vt.a + headers     [BBNDK FHS]'
	@echo '  harness     — qcc-link smoke_terminal example         [BBNDK FHS]'
	@echo '  probe       — qcc-link stepwise non-formatter probe   [BBNDK FHS]'
	@echo '  smoke       — push+run terminal smoke on Q10 (SMOKE_OK)'
	@echo '  smoke-probe — push+run API probe on Q10 (PROBE_OK before known exit trap)'
	@echo '  all         — deps->abi->lib->harness->smoke'
	@echo '  clean       — rm -rf build/'

# Explicit `path:` — this tree lives inside the Term49 repo, so a bare
# `.#deps` would resolve to the enclosing repo. `path:$(CURDIR)` treats this
# dir as the flake root.
deps:
	nix build "path:$(CURDIR)#deps" --out-link build/deps

abi-probe abi-zig lib harness probe:
	$(BB_SHELL) bash scripts/cross-build.sh $@

smoke-abi:
	bash scripts/smoke-device.sh abi

smoke:
	bash scripts/smoke-device.sh smoke

smoke-probe:
	bash scripts/smoke-device.sh probe

all: deps abi-probe abi-zig smoke-abi lib harness smoke
	@echo "libghostty-vt checks complete — see Q10 stdout above for SMOKE_OK"

clean:
	rm -rf build
