/* QNX compatibility shims for Linux-specific symbols that may be left
 * undefined by Zig-built libghostty-vt objects. These come from Zig std
 * internals, not libghostty-vt's terminal code.
 *
 *  getauxval      : Linux ELF auxv reader. Returning 0 makes Zig std take
 *                   its safe fallback paths (no AT_HWCAP/AT_RANDOM/etc.).
 *  __tls_get_addr : ELF general-dynamic TLS accessor. Single-threaded,
 *                   single-module synchronous use only needs a per-offset
 *                   slice of one static arena. */

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

/* musl/Zig CRT symbols that QNX's libc and CRT do not provide. The
 * libghostty-vt parse/render path does not depend on global constructors,
 * so no-op init/fini hooks are sufficient here. */
void _init_libc(void)     {}
void _init_array(void)    {}
void _fini_array(void)    {}
void _preinit_array(void) {}
