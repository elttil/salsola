#include <errno.h>
#include <unistd.h>

#include <todo.h>

int error_to_errno(err_t error) {
  switch (error) {
  case ERROR_READ_WOULD_BLOCK:
  case ERROR_WRITE_WOULD_BLOCK:
  case ERROR_RECVFD_WOULD_BLOCK:
  case ERROR_SENDFD_WOULD_BLOCK:
    return EWOULDBLOCK;
    break;
  default:
    return 1;
  }
}
