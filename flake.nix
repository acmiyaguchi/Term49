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
  };

  outputs = { self, bbnix, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system: {
      packages = {
        bbnix-bundle         = bbnix.packages.${system}.deploy-bundle;          # = full
        bbnix-bundle-minimal = bbnix.packages.${system}.deploy-bundle-minimal;
        bbnix-bundle-ssh     = bbnix.packages.${system}.deploy-bundle-ssh;
        bbnix-bundle-full    = bbnix.packages.${system}.deploy-bundle-full;
      };
    });
}
