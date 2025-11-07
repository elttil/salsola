#ifndef MATH_H
#define MATH_H
// TODO: I know nothing about doubles, float so most implementation
// are probably wrong.

#ifndef max
#define max(_a, _b) ((_a) > (_b) ? (_a) : (_b))
#endif // max
#ifndef min
#define min(_a, _b) ((_a) < (_b) ? (_a) : (_b))
#endif // min

#if 100 * __GNUC__ + __GNUC_MINOR__ >= 303
#define NAN __builtin_nanf("")
#define INFINITY __builtin_inff()
#else
#define NAN (0.0f / 0.0f)
#define INFINITY 1e40f
#endif

#define HUGE_VALF INFINITY
#define HUGE_VAL ((double)INFINITY)
#define HUGE_VALL ((long double)INFINITY)

double ldexp(double x, int exp);
double fmin(double x, double y);
double sqrt(double x);
double fabs(double x);
double cos(double x);
double ceil(double x);
double acos(double x);
double pow(double x, double y);
double fmod(double x, double y);
double floor(double x);
double scalbn(double, int);

// the rest is taken from musl libc
#define FP_NAN       0
#define FP_INFINITE  1
#define FP_ZERO      2
#define FP_SUBNORMAL 3
#define FP_NORMAL    4

int __fpclassify(double);
int __fpclassifyf(float);
int __fpclassifyl(long double);

static __inline unsigned __FLOAT_BITS(float __f) {
  union {
    float __f;
    unsigned __i;
  } __u;
  __u.__f = __f;
  return __u.__i;
}
static __inline unsigned long long __DOUBLE_BITS(double __f) {
  union {
    double __f;
    unsigned long long __i;
  } __u;
  __u.__f = __f;
  return __u.__i;
}

#define fpclassify(x)                                                          \
  (sizeof(x) == sizeof(float)    ? __fpclassifyf(x)                            \
   : sizeof(x) == sizeof(double) ? __fpclassify(x)                             \
                                 : __fpclassifyl(x))

#define isinf(x)                                                               \
  (sizeof(x) == sizeof(float) ? (__FLOAT_BITS(x) & 0x7fffffff) == 0x7f800000   \
   : sizeof(x) == sizeof(double)                                               \
       ? (__DOUBLE_BITS(x) & -1ULL >> 1) == 0x7ffULL << 52                     \
       : __fpclassifyl(x) == FP_INFINITE)

#define isnan(x)                                                               \
  (sizeof(x) == sizeof(float) ? (__FLOAT_BITS(x) & 0x7fffffff) > 0x7f800000    \
   : sizeof(x) == sizeof(double)                                               \
       ? (__DOUBLE_BITS(x) & -1ULL >> 1) > 0x7ffULL << 52                      \
       : __fpclassifyl(x) == FP_NAN)

#define isnormal(x)                                                            \
  (sizeof(x) == sizeof(float)                                                  \
       ? ((__FLOAT_BITS(x) + 0x00800000) & 0x7fffffff) >= 0x01000000           \
   : sizeof(x) == sizeof(double)                                               \
       ? ((__DOUBLE_BITS(x) + (1ULL << 52)) & -1ULL >> 1) >= 1ULL << 53        \
       : __fpclassifyl(x) == FP_NORMAL)

#define isfinite(x)                                                            \
  (sizeof(x) == sizeof(float) ? (__FLOAT_BITS(x) & 0x7fffffff) < 0x7f800000    \
   : sizeof(x) == sizeof(double)                                               \
       ? (__DOUBLE_BITS(x) & -1ULL >> 1) < 0x7ffULL << 52                      \
       : __fpclassifyl(x) > FP_INFINITE)
#endif // MATH_H
