#include <dirent.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>

DIR *opendir(const char *dirname) {
  int fd = open(dirname, O_RDONLY, 0);
  if (-1 == fd) {
    return NULL;
  }
  DIR *rc = malloc(sizeof(DIR));
  if (!rc) {
    return NULL;
  }
  rc->fd = fd;
  rc->internal_dirent = malloc(sizeof(struct dirent)+256);
  rc->internal_dirent_size = sizeof(struct dirent)+256;
  return rc;
}
