#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
src="$root/share/icons/icon.svg"
sizes=(86 90 96 110 144 480)

tmpdir=""
cleanup() {
  if [ -n "$tmpdir" ]; then
    rm -rf "$tmpdir"
  fi
}
trap cleanup EXIT

# Keep SVG text rendering deterministic when Nix is available: the source SVG
# requests Iosevka, and this temporary fontconfig makes librsvg see the pinned
# nixpkgs font even if it is not installed system-wide.
if command -v nix >/dev/null 2>&1; then
  font_path="$(nix build --no-link --print-out-paths nixpkgs#iosevka-bin 2>/dev/null || true)"
  if [ -n "$font_path" ]; then
    tmpdir="$(mktemp -d)"
    mkdir -p "$tmpdir/cache"
    cat > "$tmpdir/fonts.conf" <<EOF
<?xml version="1.0"?>
<!DOCTYPE fontconfig SYSTEM "fonts.dtd">
<fontconfig>
  <dir>$font_path/share/fonts/truetype</dir>
  <cachedir>$tmpdir/cache</cachedir>
</fontconfig>
EOF
    export FONTCONFIG_FILE="$tmpdir/fonts.conf"
  fi
fi

if command -v rsvg-convert >/dev/null 2>&1; then
  rsvg=(rsvg-convert)
elif command -v nix >/dev/null 2>&1; then
  rsvg=(nix shell nixpkgs#librsvg -c rsvg-convert)
else
  echo "error: rsvg-convert not found; install librsvg or Nix" >&2
  exit 1
fi

for size in "${sizes[@]}"; do
  out="$root/share/icons/icon-${size}x${size}.png"
  "${rsvg[@]}" --width "$size" --height "$size" --format png --output "$out" "$src"
  echo "generated ${out#$root/}"
done
