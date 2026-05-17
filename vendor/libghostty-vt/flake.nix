{
  description = "libghostty-vt BB10/QNX integration — pin a Zig toolchain for cross-building Ghostty's libc-free VT core to ARM-EABI, linked into BB10/QNX by BBNDK qcc";

  # No upstream flake to follow (unlike fen-blackberry, which followed fen's
  # nixpkgs). ghostty itself is the `vendor/ghostty` git submodule, consumed
  # directly by cross-build.sh — Nix here only materializes a *pinned* Zig at
  # a stable path so the BBNDK FHS shell (no nix devShell PATH) can find it.
  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let pkgs = import nixpkgs { inherit system; };
      in {
        # Pure, reproducible: stable-path Zig toolchain. The qcc cross-compile
        # itself runs OUTSIDE Nix in the parent BBNDK FHS (see Makefile).
        packages.deps = import ./nix/deps.nix { inherit pkgs; };
        packages.default = self.packages.${system}.deps;

        # Host fallback if Zig won't run inside the BBNDK FHS chroot
        # (run zig steps here, qcc steps in FHS — like fen's stage3 split).
        devShells.default = pkgs.mkShell {
          # zig_0_15 == 0.15.2, the exact version ghostty cf24a48 requires.
          packages = [ pkgs.zig_0_15 pkgs.git pkgs.gnumake pkgs.coreutils pkgs.binutils ];
        };
      });
}
