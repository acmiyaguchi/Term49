// Zig side of the mixed ABI check. Mirrors abi_probe.c's signatures so
// abi_main.c (qcc-compiled) calls this freestanding Zig object across the
// C ABI boundary. If the float / struct passing convention differs between
// the Zig target triple and qcc's, the on-device run fails ABI_OK.
//
// Build: zig build-obj -target <triple> -mcpu cortex_a9 -OReleaseSmall
// Zig 0.16. ReleaseSmall + trivial math + build-obj ⇒ no safety checks,
// so no panic handler is referenced. If a future change makes the build
// demand one, add the 0.16 form:
//   const std = @import("std");
//   pub const panic = std.debug.FullPanic(struct {
//       pub fn p(_: []const u8, _: ?usize) noreturn { @trap(); }
//   }.p);

const Vec3 = extern struct { x: f32, y: f32, z: f32 };

export fn abi_add_d(a: f64, b: f64) f64 {
    return a + b;
}
export fn abi_add_f(a: f32, b: f32) f32 {
    return a + b;
}
export fn abi_vec_sum(v: Vec3) f32 {
    return v.x + v.y + v.z;
}
export fn abi_mix(i: i32, d: f64) f64 {
    return @as(f64, @floatFromInt(i)) + d;
}
export fn abi_add_i(a: i32, b: i32) i32 {
    return a + b;
}
