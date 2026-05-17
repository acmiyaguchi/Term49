/* Gate G shims — the bounded glue for the two Linux-isms the
 * arm-linux-musleabi build of libghostty-vt leaves undefined and QNX libc
 * does not provide (Gate F finding). Both come from Zig std internals, NOT
 * libghostty-vt's own code (no `threadlocal` anywhere in src/terminal).
 * This is the plan's anticipated "stub + one bounded patch" (K4).
 *
 *  getauxval      : Linux ELF auxv reader. Returning 0 makes Zig std take
 *                   its safe fallback paths (no AT_HWCAP/AT_RANDOM/etc.).
 *  __tls_get_addr : ELF general-dynamic TLS accessor (emitted because the
 *                   lib is built -fPIC). Single-threaded, single-module
 *                   synchronous use ⇒ a per-offset slice of one static
 *                   arena is sufficient. If a device run faults in TLS,
 *                   escalate to forcing local-exec TLS via a ghostty
 *                   build patch (patches/). */

#include <stddef.h>

unsigned long getauxval(unsigned long type) {
    (void)type;
    return 0;
}

struct ghostty_tls_index {
    unsigned long ti_module;
    unsigned long ti_offset;
};

static unsigned char ghostty_tls_arena[1u << 16]; /* 64 KiB, zeroed */

void *__tls_get_addr(void *arg) {
    struct ghostty_tls_index *ti = (struct ghostty_tls_index *)arg;
    unsigned long off = ti ? ti->ti_offset : 0;
    if (off >= sizeof(ghostty_tls_arena)) off = 0;
    return &ghostty_tls_arena[off];
}

/* musl/Zig-CRT-isms baked into the arm-linux-musleabi .a that QNX's libc
 * and CRT do not provide (Gate G device-load finding: ldqnx FATAL on these
 * 4 non-weak UNDs). libghostty-vt has no global ctors we depend on for the
 * parse path, so no-op CRT init is the bounded resolution. The frame-info /
 * _Jv_ refs are weak and resolve to 0 (non-fatal) so are left alone. */
void _init_libc(void)     {}
void _init_array(void)    {}
void _fini_array(void)    {}
void _preinit_array(void) {}
