#include "protocol/rtsp/rtsp.h"
#include "platform/utilities/rtsp_message_utilities.h"
#include <assert.h>

int main(void) {
  rtsp_message *message = msg_init();
  assert(message != NULL);
  msg_free(&message);
  assert(message == NULL);
  msg_free(&message);
}
