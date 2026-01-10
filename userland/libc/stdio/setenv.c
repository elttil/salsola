#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <ctype.h>
#include <fcntl.h>

int setenv(const char *name, const char *value, int overwrite) {
	for(const char *s = name;*s;s++) {
		if(!isalnum(*s)) {
			errno = EINVAL;
			return -1;
		}
	}

	char path[256];
	int rc = snprintf(path, sizeof(path), "/env/%s", name);
	struct sv sv_path = sv_init(path, rc);
	
	int flags = O_WRITE;
	if(!overwrite) {
		flags |= O_CREAT;
	}

	int fd;
	err_t err = sa_open(&fd, sv_path, flags, 0);
	if(err != ERROR_SUCCESS) {
		if(ERROR_NO_MEMORY == err) {
			errno = ENOMEM;
			return -1;
		}
		return 0;
	}
	
	// TODO: Surely there is a better way. (Right?)
	dprintf(fd, "%s", value);
	close(fd);
	return 0;
}
