#undef main
#include "common.h"
#include "rtsp.h"
#include "utilities/rtsp_message_utilities.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
  rtsp_conn_info conn = {0};
  rtsp_message req = {0};
  rtsp_message *resp = msg_init();
  strcpy(req.method, "OPTIONS");
  rtsp_dispatch_request(&conn, &req, resp);
  assert(resp->respcode == 200);
  const char *methods = msg_get_header(resp, "Public");
  assert(methods != NULL);
  assert(strstr(methods, "ANNOUNCE") == NULL);
  assert(strstr(methods, "FLUSHBUFFERED") != NULL);
  assert(strstr(methods, "GET_PARAMETER") != NULL);
  msg_free(&resp);
  const char *unsupported[] = {"ANNOUNCE", "PAUSE", "UNSUPPORTED"};
  for (size_t index = 0; index < sizeof(unsupported) / sizeof(unsupported[0]); index++) {
    resp = msg_init();
    strcpy(req.method, unsupported[index]);
    rtsp_dispatch_request(&conn, &req, resp);
    assert(resp->respcode == 501);
    msg_free(&resp);
  }
  puts("Only AirPlay 2 methods advertised.");
  return 0;
}
