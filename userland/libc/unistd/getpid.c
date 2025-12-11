#include <fcntl.h>
#include <tb/sv.h>
#include <unistd.h>

pid_t getpid(void) {
  int fd = open("/proc/self/pid", O_RDONLY);
  if (-1 == fd) {
    return -1;
  }

  int rc;
  char buffer[64];
  if (-1 == (rc = read(fd, buffer, sizeof(buffer)))) {
    close(fd);
    return -1;
  }

  struct sv s = sv_init(buffer, rc);
  int got_num;
  u64 r = sv_parse_unsigned_number(s, NULL, &got_num);
  if (!got_num) {
    // TODO: What should this be?
    errno = EBADMSG;
    close(fd);
    return -1;
  }
  close(fd);
  return r;
}
