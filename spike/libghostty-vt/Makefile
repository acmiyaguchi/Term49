# libghostty-vt feasibility spike — cross-build + on-device gates.
#
# Nix only MATERIALIZES a pinned Zig (+ ghostty src) via `make deps`; the
# qcc compile/link + readelf run OUTSIDE Nix inside the parent BBNDK FHS
# shell (Zig is invoked there by absolute path). Mirrors fen-blackberry.
#
# Gate map (see ~/.claude/plans/okay-in-the-worktree-eager-whisper.md):
#   abi-probe = Gate D   abi-zig+smoke-abi = Gate E (decisive)
#   lib       = Gate F   harness+smoke     = Gate G

include config.mk

.PHONY: help deps abi-probe abi-zig smoke-abi lib harness smoke spike clean

help:
	@echo 'libghostty-vt spike targets:'
	@echo '  deps       — nix build .#deps -> build/deps (pinned zig + ghostty src)'
	@echo '  abi-probe  — Gate D: qcc readelf -A -> pick Zig triple   [BBNDK FHS]'
	@echo '  abi-zig    — Gate E: zig lib + qcc-link ABI probe binary  [BBNDK FHS]'
	@echo '  smoke-abi  — Gate E: push+run abi probe on Q10 (ABI_OK)'
	@echo '  lib        — Gate F: zig build libghostty-vt.a + header   [BBNDK FHS]'
	@echo '  harness    — Gate G: qcc-link spike_main vs the .a        [BBNDK FHS]'
	@echo '  smoke      — Gate G: push+run spike on Q10 (SPIKE_OK)'
	@echo '  spike      — full chain: deps->D->E->F->G'
	@echo '  clean      — rm -rf build/'

# Explicit `path:` — this spike lives INSIDE the Term49 worktree (not its
# own git repo, unlike fen-blackberry), so a bare `.#deps` would resolve to
# the enclosing repo. `path:$(CURDIR)` treats this dir as the flake root.
deps:
	nix build "path:$(CURDIR)#deps" --out-link build/deps

abi-probe abi-zig lib harness:
	$(BB_SHELL) bash scripts/cross-build.sh $@

smoke-abi:
	bash scripts/smoke-device.sh abi

smoke:
	bash scripts/smoke-device.sh spike

spike: deps abi-probe abi-zig smoke-abi lib harness smoke
	@echo "SPIKE COMPLETE — see Q10 stdout above for SPIKE_OK"

clean:
	rm -rf build
