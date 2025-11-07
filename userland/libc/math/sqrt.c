#include <math.h>

double sqrt(double x) {
  double guess = x;

  // TODO: This is probably not optimal.
  // TODO: How many iterations should be made?
  for (int i = 0; i < 7; i++) {
    guess = 0.5 * (guess + (x / guess));
  }
  return guess;
}
