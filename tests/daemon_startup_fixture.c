#include <pulse/pulseaudio.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>

static char loop_storage, api_storage, context_storage;
static unsigned loops_created, loops_released, contexts_created, contexts_released;
static pa_context_state_t context_state = PA_CONTEXT_UNCONNECTED;

pa_threaded_mainloop *pa_threaded_mainloop_new(void) {
  ++loops_created;
  return (pa_threaded_mainloop *)&loop_storage;
}
pa_mainloop_api *pa_threaded_mainloop_get_api(pa_threaded_mainloop *loop) {
  (void)loop;
  return (pa_mainloop_api *)&api_storage;
}
pa_context *pa_context_new(pa_mainloop_api *api, const char *name) {
  (void)api;
  (void)name;
  ++contexts_created;
  return (pa_context *)&context_storage;
}
void pa_context_set_state_callback(pa_context *context, pa_context_notify_cb_t callback, void *data) {
  (void)context;
  (void)callback;
  (void)data;
}
void pa_threaded_mainloop_lock(pa_threaded_mainloop *loop) { (void)loop; }
void pa_threaded_mainloop_unlock(pa_threaded_mainloop *loop) { (void)loop; }
int pa_threaded_mainloop_start(pa_threaded_mainloop *loop) { (void)loop; return 0; }
void pa_threaded_mainloop_stop(pa_threaded_mainloop *loop) { (void)loop; }
void pa_threaded_mainloop_free(pa_threaded_mainloop *loop) { (void)loop; ++loops_released; }
void pa_context_unref(pa_context *context) { (void)context; ++contexts_released; }
void pa_context_disconnect(pa_context *context) { (void)context; }
pa_context_state_t pa_context_get_state(const pa_context *context) { (void)context; return context_state; }
int pa_context_errno(const pa_context *context) { (void)context; return PA_ERR_CONNECTIONREFUSED; }
int pa_context_connect(pa_context *context, const char *server, pa_context_flags_t flags,
                       const pa_spawn_api *api) {
  (void)context;
  (void)server;
  (void)flags;
  (void)api;
  const char *mode = getenv("DAEMON_STARTUP_MODE");
  if (strcmp(mode, "connect-failure") == 0)
    return -1;
  if (strcmp(mode, "context-failure") == 0) {
    context_state = PA_CONTEXT_FAILED;
    return 0;
  }
  fputs("Awaiting startup signal\n", stderr);
  fflush(stderr);
  for (;;) pause();
}
__attribute__((destructor)) static void verify_cleanup(void) {
  if (loops_created != 1 || loops_released != 1 || contexts_created != 1 || contexts_released != 1) {
    fputs("PulseAudio startup resources leaked\n", stderr);
    _Exit(98);
  }
  fputs("PulseAudio startup resources released\n", stderr);
}
