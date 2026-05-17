/* qcc ABI probe. Compiled with the exact Term49 compiler
 * (qcc -V4.6.3,gcc_ntoarmv7le) so `readelf -A` can report the ARM EABI
 * attributes (Tag_ABI_VFP_args et al.) that the Zig target must match.
 * Float, double, struct-with-float, and mixed int/float calls force the
 * compiler to commit to its float-passing convention. */
typedef struct { float x, y, z; } Vec3;

double p_add_d(double a, double b) { return a + b; }
float  p_add_f(float a, float b)   { return a + b; }
float  p_vec_sum(Vec3 v)           { return v.x + v.y + v.z; }
double p_mix(int i, double d)      { return (double)i + d; }
int    p_add_i(int a, int b)       { return a + b; }
