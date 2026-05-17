/* qcc side of the mixed ABI check. Built with
 * qcc -V4.6.3,gcc_ntoarmv7le (Term49's exact compiler), linked against the
 * zig-compiled abi_zig.o, and run on the Q10. Exact results confirm that
 * Zig and qcc agree on the C ABI at this boundary. */
#include <stdio.h>

typedef struct { float x, y, z; } Vec3;

extern double abi_add_d(double, double);
extern float  abi_add_f(float, float);
extern float  abi_vec_sum(Vec3);
extern double abi_mix(int, double);
extern int    abi_add_i(int, int);

int main(void) {
    int ok = 1;
    double d = abi_add_d(1.5, 2.25);           ok &= (d == 3.75);
    float  f = abi_add_f(0.5f, 0.25f);         ok &= (f == 0.75f);
    Vec3   v = { 1.0f, 2.0f, 3.0f };
    float  s = abi_vec_sum(v);                 ok &= (s == 6.0f);
    double m = abi_mix(7, 0.5);                ok &= (m == 7.5);
    int    i = abi_add_i(40, 2);               ok &= (i == 42);

    printf("abi_add_d=%.4f abi_add_f=%.4f abi_vec_sum=%.4f abi_mix=%.4f abi_add_i=%d\n",
           d, (double)f, (double)s, m, i);
    if (ok) { printf("ABI_OK\n"); return 0; }
    printf("ABI_FAIL\n");
    return 1;
}
