#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

#include <fcntl.h>
#include <stdio.h>

int system(const char *command) {
  if (!command) {
    return -1;
  }
   int sfd = open("/dev/serial", O_WRITE);
   dprintf(sfd, "command: %s\n", command);
   close(sfd);
  int pid = fork();
  if (0 == pid) {
    char *argv[2];
    argv[0] = "/bin/sh";
    argv[1] = (char *)command;
    execv("/bin/sh", argv);
  }
  // FIXME: Use waitpid
  int rc;
  (void)wait(&rc);
  return rc;
}
