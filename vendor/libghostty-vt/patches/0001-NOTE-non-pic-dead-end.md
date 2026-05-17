# 0001 — non-PIC / local-exec TLS: DEAD END (not applied)

Kept as a numbered note so nobody re-tries this.

## What it was

During the **musl rung** (`arm-linux-musleabi`), Zig std dragged in a
libc-model thread-local `errno`, so libghostty-vt emitted general-dynamic
TLS + `__tls_get_addr` + dynamic-TLS relocations. QNX 6.6's `ldqnx.so.2`
cannot process those, aborting at load with 4 nameless "unknown symbol"
FATALs. The attempted fix was patching `GhosttyLibVt.zig` `initLib` to
build the static lib **non-PIC**, forcing LLVM to local-exec TLS.

## Why it is a dead end

It "worked" only in the sense of getting past the loader FATAL — but
non-PIC code linked into a QNX PIE produced `DT_TEXTREL`, which `ldqnx`
mishandles, and that was the cause of the subsequent runtime SIGSEGV.

The real fix was not "make TLS work" but **stop dragging in libc at all**:
move off the musl rung back to `arm-freestanding-eabi` and shed the
Linux/musl std substrate (patches 0002–0007). With zero libc/errno there
is **zero TLS**, so the GD-TLS problem disappears entirely and PIC is
fine — exactly like libghostty-vt's own wasm/freestanding configuration,
which is PIC-clean. `GhosttyLibVt.zig` is therefore left **pristine**
(`lib.root_module.pic = true`, upstream default); there is no 0001 patch.

## Lesson

Every freestanding blocker was a ghostty OS-less carve-out scoped to
`isWasm()` only. The correct move is to generalize each to
`os.tag == .freestanding` (patches 0003/0005/0006), not to invent
QNX-specific TLS/PIC hacks. "Be like the wasm config."
