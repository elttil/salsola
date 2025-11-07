#include <errno.h>
#include <stdio.h>

// https://pubs.opengroup.org/onlinepubs/9699919799/functions/perror.html
void perror(const char *s) {
  // The perror() function shall map the error number accessed through the
  // symbol errno to a language-dependent error message, which shall be written
  // to the standard error stream as follows:

  // (First (if s is not a null pointer and the character pointed to
  // by s is not the null byte),
  if (s && *s != '\0') {
    // the string pointed to by s
    // followed by a <colon> and a <space>.
    printf("%s: ", s);
  }

  // Then an error message string followed by a <newline>.
  // The contents of the error message strings shall be the same as those
  // returned by strerror() with argument errno.
  printf("%s\n", strerror(errno));
}
