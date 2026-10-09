#include "common.h"
#include "rtsp.h"
#include "rtsp_message.hpp"
#include "utilities/rtsp_message_utilities.h"
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

int msg_write_response(rtsp_conn_info *conn, rtsp_message *response);

static int readLine(rtsp_message **message, const std::string &line) {
  std::string editable = line;
  return msg_handle_line(message, editable.data());
}

static void checkRequestParsing() {
  rtsp_message *message = nullptr;
  assert(readLine(&message, "  OPTIONS  /info  RTSP/1.0") == -1);
  assert(std::strcmp(message->method, "OPTIONS") == 0);
  assert(std::strcmp(message->path, "/info") == 0);
  assert(readLine(&message, "Content-Length: 3") == -1);
  assert(readLine(&message, "") == 3);
  msg_free(&message);
  message = nullptr;
  assert(readLine(&message, std::string(30, 'M') + " /" + std::string(300, 'p') + " HTTP/1.1") == -1);
  assert(std::strlen(message->method) == 15);
  assert(std::strlen(message->path) == 255);
  msg_free(&message);
  message = nullptr;
  assert(readLine(&message, "OPTIONS /info RTSP/2.0") == 0);
  assert(message == nullptr);
  assert(readLine(&message, "OPTIONS /info RTSP/1.0") == -1);
  assert(readLine(&message, "Broken:header") == 0);
  assert(message == nullptr);
}

static void checkHeaderLimitAndDuplicates() {
  rtsp_message *message = msg_init();
  assert(msg_add_header(message, "CSeq", "first") == 0);
  assert(msg_add_header(message, "cseq", "second") == 0);
  assert(std::strcmp(msg_get_header(message, "CSEQ"), "first") == 0);
  for (int index = 2; index < 16; ++index)
    assert(msg_add_header(message, "Repeated", "value") == 0);
  assert(msg_add_header(message, "Overflow", "ignored") == 1);
  assert(msg_get_header(message, "Overflow") == nullptr);
  msg_free(&message);
}

static void checkBinaryResponseFraming() {
  int sockets[2];
  assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
  rtsp_conn_info connection{};
  connection.fd = sockets[0];
  rtsp_message *response = msg_init();
  response->respcode = 200;
  assert(msg_write_response(&connection, response) == 0);
  char received[256];
  auto count = read(sockets[1], received, sizeof(received));
  assert(std::string(received, count) == "RTSP/1.0 200 OK\r\nContent-Length: 0\r\n\r\n");
  msg_add_header(response, "CSeq", "1");
  msg_add_header(response, "CSeq", "2");
  response->content = static_cast<char *>(malloc(3));
  response->contentlength = 3;
  memcpy(response->content, "A\0B", 3);
  assert(msg_write_response(&connection, response) == 0);
  count = read(sockets[1], received, sizeof(received));
  std::string expected = "RTSP/1.0 200 OK\r\nCSeq: 1\r\nCSeq: 2\r\nContent-Length: 3\r\n\r\n";
  expected.append("A\0B", 3);
  assert(std::string(received, count) == expected);
  msg_free(&response);
  close(sockets[0]);
  close(sockets[1]);
}

int main() {
  checkRequestParsing();
  checkHeaderLimitAndDuplicates();
  checkBinaryResponseFraming();
  RtspMessage owned;
  assert(owned.readLine("OPTIONS /info RTSP/1.0") == -1);
  assert(owned.requestsMethod("OPTIONS"));
  assert(owned.requestsPath("/info"));
  assert(owned.readLine("Content-Length: 3") == -1);
  assert(owned.readLine("") == 3);
  std::string borrowed("A\0B", 3);
  owned.replaceBody(borrowed);
  borrowed[0] = 'X';
  assert(owned.bodyText() == std::string_view("A\0B", 3));
  assert(owned.bodyData()[3] == '\0');
  owned.replaceBody("");
  assert(owned.bodyLength() == 0);
}
