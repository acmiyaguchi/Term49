#!/usr/bin/env python3
"""Deduplicate the busybox applet copies in a staged bbnix bin/ directory.

The bbnix bundle ships one busybox ELF plus ~131 applet symlinks (ls -> busybox,
etc.), but `make stage-bbnix` copies it with `cp -RL`, dereferencing every
symlink into a full ~263 KB copy. blackberry-nativepackager then bundles all
~132 byte-identical copies into the .bar (~23 MB / ~55% of the download), and
the BAR format has no symlink concept so the copies cannot be collapsed in the
package (an earlier zip-symlink experiment was proven to brick the userland --
the BB10 installer writes zip symlinks as plain target-text files).

So we dedup at the staging tree instead: keep the single busybox, delete every
byte-identical copy, and record the deleted applet names to APPLETS_OUT. At
runtime Term50 recreates them as real symlinks in a writable PATH dir
(bbnix_install_applets() in src/main.c), so the applet set the user sees is
unchanged.

Usage: stage-dedup-busybox.py <bindir> <applets_out>
"""
import hashlib
import os
import sys


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 16), b""):
            h.update(chunk)
    return h.digest()


def main(bindir, applets_out):
    busybox = os.path.join(bindir, "busybox")
    if not os.path.isfile(busybox):
        # No busybox in this bundle variant -- nothing to dedup.
        print(f"stage-dedup-busybox: no {busybox}, skipping", file=sys.stderr)
        # Still emit an (empty) list so the bundle is self-consistent.
        _write_list(applets_out, [])
        return 0

    bb_size = os.path.getsize(busybox)
    bb_hash = sha256(busybox)

    removed = []
    freed = 0
    for name in os.listdir(bindir):
        if name == "busybox":
            continue
        path = os.path.join(bindir, name)
        # Only collapse real, regular files that are byte-identical to busybox.
        # The 9 real binaries (curl, ssh, tmux, zsh, ...) differ and are kept;
        # symlinks (none expected after cp -RL, but be defensive) are left alone.
        if os.path.islink(path) or not os.path.isfile(path):
            continue
        if os.path.getsize(path) != bb_size:
            continue
        if sha256(path) != bb_hash:
            continue
        os.remove(path)
        removed.append(name)
        freed += bb_size

    _write_list(applets_out, removed)
    print(
        f"stage-dedup-busybox: removed {len(removed)} busybox copies "
        f"({freed // (1 << 20)} MiB freed), kept busybox + "
        f"{len(os.listdir(bindir)) - 1} other binaries; "
        f"applet list -> {applets_out}",
        file=sys.stderr,
    )
    return 0


def _write_list(applets_out, names):
    os.makedirs(os.path.dirname(applets_out), exist_ok=True)
    with open(applets_out, "w") as f:
        for name in sorted(names):
            f.write(name + "\n")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print(__doc__, file=sys.stderr)
        sys.exit(2)
    sys.exit(main(sys.argv[1], sys.argv[2]))
