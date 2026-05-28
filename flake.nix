{
  description = "Term50 packaging helpers — optional bbnix userland bundle";

  # Term50 itself is built with the BlackBerry NDK via the Makefile, not Nix.
  # This flake exists only to pin the bbnix userland and re-expose its
  # relocatable deploy bundle so `make stage-bbnix` can build a known revision.
  # bbnix builds are impure: they read $BBNIX_SYSROOT, so the Makefile invokes
  # `nix build --impure`.
  inputs = {
    bbnix.url = "github:acmiyaguchi/bbnix";
    flake-utils.follows = "bbnix/flake-utils";
    nixpkgs.follows = "bbnix/nixpkgs";
  };

  outputs = { self, bbnix, flake-utils, nixpkgs }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = import nixpkgs { inherit system; };
        iosevkaTerm = pkgs.iosevka-bin.override { variant = "SGr-IosevkaTerm"; };
        term50FontsBundle = pkgs.runCommand "term50-fonts-bundle" {
          nativeBuildInputs = [ (pkgs.python3.withPackages (ps: [ ps.fonttools ])) ];
        } ''
          set -eu
          mkdir -p "$out" "$out/LICENSES"

          python - ${iosevkaTerm}/share/fonts/truetype/SGr-IosevkaTerm-Regular.ttc \
            "$out/IosevkaTerm-Regular.ttf" <<'PY'
import sys
from fontTools.ttLib import TTCollection
TTCollection(sys.argv[1]).fonts[0].save(sys.argv[2])
PY
          install -m444 ${pkgs.jetbrains-mono}/share/fonts/truetype/JetBrainsMono-Regular.ttf \
            "$out/JetBrainsMono-Regular.ttf"
          install -m444 ${pkgs.source-code-pro}/share/fonts/opentype/SourceCodePro-Regular.otf \
            "$out/SourceCodePro-Regular.otf"
          install -m444 ${pkgs.ubuntu-classic}/share/fonts/truetype/ubuntu/UbuntuMono-R.ttf \
            "$out/UbuntuMono-R.ttf"

          ofl=$(find ${pkgs.jetbrains-mono.src} -maxdepth 3 -type f \
            \( -iname 'OFL.txt' -o -iname 'LICENSE*' \) -print -quit)
          if [ -n "$ofl" ]; then
            install -m444 "$ofl" "$out/LICENSES/OFL-1.1.txt"
          fi
          ufl=$(find ${pkgs.ubuntu-classic.src} -maxdepth 3 -type f \
            \( -iname 'LICENCE*' -o -iname 'LICENSE*' \) -print -quit)
          if [ -n "$ufl" ]; then
            install -m444 "$ufl" "$out/LICENSES/Ubuntu-Font-License-1.0.txt"
          fi

          cat > "$out/FONT_SOURCES.txt" <<EOF
Term50 bundled fonts, staged by Nix from nixpkgs.

Files:
- IosevkaTerm-Regular.ttf     Iosevka Term, SIL Open Font License 1.1
- JetBrainsMono-Regular.ttf   JetBrains Mono, SIL Open Font License 1.1
- SourceCodePro-Regular.otf   Source Code Pro, SIL Open Font License 1.1
- UbuntuMono-R.ttf            Ubuntu Mono, Ubuntu Font License 1.0

Nix packages:
- iosevka-bin override { variant = "SGr-IosevkaTerm"; }
- jetbrains-mono
- source-code-pro
- ubuntu-classic

The license texts copied into LICENSES/ apply to the staged font files.
EOF
        '';
      in {
      packages = {
        bbnix-bundle         = bbnix.packages.${system}.deploy-bundle;          # = full
        bbnix-bundle-minimal = bbnix.packages.${system}.deploy-bundle-minimal;
        bbnix-bundle-ssh     = bbnix.packages.${system}.deploy-bundle-ssh;
        bbnix-bundle-full    = bbnix.packages.${system}.deploy-bundle-full;

        term50-fonts-bundle = term50FontsBundle;
      };
    });
}
