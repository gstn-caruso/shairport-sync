#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <pulse/pulseaudio.h>
#include <avahi-client/client.h>
#include <avahi-common/thread-watch.h>

static void unexpected_service(void) {
  fputs("configuration validation started an external service\n", stderr);
  exit(99);
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
