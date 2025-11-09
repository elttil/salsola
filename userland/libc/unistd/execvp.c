#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int execvp(const char *file, char *const argv[]) {
  if ('/' == *file) {
    return execv(file, argv);
  }

  char *p = getenv("PATH");
  if (!p) {
    errno = ENOENT;
    return -1;
  }

  struct sv paths = C_TO_SV(p);

  struct sb builder;
  sb_init(&builder);
  for (;;) {
    struct sv path = sv_split_delim(paths, &paths, ':');
    if (0 == sv_length(path)) {
      break;
    }
    sb_reset(&builder);
    sb_append_sv(&builder, path);
    sb_append(&builder, "/");
    sb_append(&builder, file);
    sb_append_buffer(&builder, "\0", 1);

    if (-1 == execv(builder.string, argv)) {
      continue;
    }
  }
  sb_free(&builder);
  errno = ENOENT;
  return -1;
}
