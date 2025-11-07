#include <math.h>

double fabs(double x) {
  if (x < 0.0f) {
    return -1.0f * x;
  }
  return x;
}
