#include <typedefs.h>
#include <stddef.h>

typedef struct __dir {
  u64 fd;
  struct dirent *internal_dirent;
  size_t internal_dirent_size;
} DIR;

struct vfs_dirent {
  u32 d_ino;
  u16 d_namelength;
  char d_name[1];
};

struct dirent {
  u32 d_ino;
  u16 d_namelength;
  char d_name[256];
};

DIR *opendir(const char *dirname);
int closedir(DIR *dir);
struct dirent *readdir(DIR *dir);
