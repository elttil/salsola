#include <dirent.h>
#include <unistd.h>
#include <stdlib.h>

int closedir(DIR *dir) {
  close(dir->fd);
  free(dir->internal_dirent);
  free(dir);
  return 0;
}
