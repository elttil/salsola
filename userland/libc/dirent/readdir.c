#include <dirent.h>
#include <stdio.h>
#include <unistd.h>
#include <assert.h>
#include <stdlib.h>

#include <syscall.h>
#include <syscalls.h>

err_t getdent(u64 fd, struct dirent *dirp, size_t dir_entry_size,
                      u64 nentries, u64 *rc) {
  return syscall(SYS_GETDENT, fd, (u64)dirp, dir_entry_size, nentries, (u64)rc);
}

struct dirent *readdir(DIR *dir) {
  for(;;) {
	  size_t rc;
	  err_t err = getdent(dir->fd, dir->internal_dirent, dir->internal_dirent_size, 1, &rc);
	  if(ERROR_BUFFER_TOO_SMALL == err) {
		dir->internal_dirent_size += 256;
		dir->internal_dirent = realloc(dir->internal_dirent, dir->internal_dirent_size);
        assert(dir->internal_dirent); // TODO
        continue;
	  }
     if(ERROR_SUCCESS == err) {
		return dir->internal_dirent;
     }
	 return NULL;
  }
}
