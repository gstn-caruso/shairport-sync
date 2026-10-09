#define _GNU_SOURCE
#include "nqptp-shm-structures.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <unistd.h>

int shm_open(const char *name, int flags, mode_t mode) {
  (void)name;
  (void)flags;
  (void)mode;
  const char *version = getenv("NQPTP_TEST_VERSION");
  if (strcmp(version, "missing") == 0 || strcmp(version, "permission") == 0) {
    errno = strcmp(version, "missing") == 0 ? ENOENT : EACCES;
    return -1;
  }
  int fd = memfd_create("nqptp-test", 0);
  struct shm_structure data = {0};
  data.version = atoi(version);
  if (strcmp(version, "inconsistent") == 0) {
    data.version = NQPTP_SHM_STRUCTURES_VERSION;
    data.main.master_clock_id = 1;
  }
  size_t length = strcmp(version, "truncated") == 0 ? 1 : sizeof(data);
  if (write(fd, &data, length) != (ssize_t)length) {
    close(fd);
    return -1;
  }
  return fd;
}

ssize_t sendto(int fd, const void *buffer, size_t length, int flags,
               const struct sockaddr *address, socklen_t address_length) {
  (void)fd;
  (void)buffer;
  (void)flags;
  (void)address;
  (void)address_length;
  return length;
}
