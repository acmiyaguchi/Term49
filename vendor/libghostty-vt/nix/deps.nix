# Pure source/toolchain materialization (mirrors fen-blackberry/nix/deps.nix
# in spirit). Collects, into one stable out path, what cross-build.sh needs
# but cannot get from the BBNDK FHS shell:
#
#   zig/        <- pinned Zig toolchain (zig/bin/zig)   [Steps E, F]
#   VERSIONS    <- provenance for assertion
#
# ghostty itself is the vendor/ghostty git submodule, read directly by
# cross-build.sh — not materialized here (it is large and pinned by SHA).
{ pkgs }:

# ghostty cf24a48 pins EXACTLY Zig 0.15.2 (build.zig.zon minimum_zig_version
# + requireZig guard; nixpkgs default `zig` is 0.16.0 and is rejected).
# pkgs.zig_0_15 == 0.15.2 in the locked nixpkgs.
let zig = pkgs.zig_0_15;
in
pkgs.runCommand "libghostty-vt-bb10-deps"
{
  passthru.versions = { zig = zig.version; };
} ''
  set -eu
  mkdir -p "$out"
  ln -s ${zig} "$out/zig"               # -> $out/zig/bin/zig
  cat > "$out/VERSIONS" <<EOF
zig=${zig.version}
EOF
''
