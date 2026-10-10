#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <dlfcn.h>
#include <sys/mman.h>
#include <pulse/pulseaudio.h>
#include <avahi-client/client.h>
#include <avahi-common/thread-watch.h>

static void unexpected_service(void) {
  fputs("configuration validation started an external service\n", stderr);
  exit(99);
}

char *realpath(const char *path, char *resolved) {
  const char *unresolvable = getenv("DAEMON_TEST_UNRESOLVABLE_PATH");
  if (unresolvable && strcmp(path, unresolvable) == 0) {
    errno = ENOENT;
    return NULL;
  }
  const char *expected = getenv("DAEMON_TEST_DEFAULT_PATH");
  if (expected) {
    if (strcmp(path, expected) != 0) {
      fputs("default configuration path depends on executable name\n", stderr);
      exit(99);
    }
    errno = getenv("DAEMON_TEST_DEFAULT_UNREADABLE") ? EACCES : ENOENT;
    return NULL;
  }
  char *(*original)(const char *, char *) = dlsym(RTLD_NEXT, "realpath");
  return original(path, resolved);
}

pa_threaded_mainloop *pa_threaded_mainloop_new(void) {
  unexpected_service();
  return NULL;
}

AvahiThreadedPoll *avahi_threaded_poll_new(void) {
  unexpected_service();
  return NULL;
}

int shm_open(const char *name, int flags, mode_t mode) {
  (void)name;
  (void)flags;
  (void)mode;
  unexpected_service();
  return -1;
}
