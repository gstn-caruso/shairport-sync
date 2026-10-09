#include "session/session_state.hpp"
#include "runtime/common.h"
#include "protocol/rtsp/rtsp.h"
#include "protocol/rtsp/rtsp_message.hpp"
#include "platform/utilities/rtsp_message_utilities.h"
#include "platform/utilities/debug.h"
#include <gtest/gtest.h>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <string>
#include <chrono>
#include <thread>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

static unsigned releasedPlists = 0;
extern "C" void __real_plist_free(plist_t plist);
extern "C" void __wrap_plist_free(plist_t plist) {
  ++releasedPlists;
  __real_plist_free(plist);
}

static int readLine(rtsp_message **message, const std::string &line) {
  if (!*message)
    *message = msg_init();
  auto parsed = (*message)->readLine(line);
  if (!parsed) {
    msg_free(message);
    return 0;
  }
  return *parsed;
}

static void checkRequestParsing() {
  rtsp_message *message = nullptr;
  assert(readLine(&message, "  OPTIONS  /info  RTSP/1.0") == -1);
  assert(std::strcmp(message->methodName(), "OPTIONS") == 0);
  assert(std::strcmp(message->requestPath(), "/info") == 0);
  assert(readLine(&message, "Content-Length: 3") == -1);
  assert(readLine(&message, "") == 3);
  msg_free(&message);
  message = nullptr;
  assert(readLine(&message, std::string(30, 'M') + " /" + std::string(300, 'p') + " HTTP/1.1") == -1);
  assert(std::strlen(message->methodName()) == 15);
  assert(std::strlen(message->requestPath()) == 255);
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
  assert(message->addHeader("CSeq", "first"));
  assert(message->addHeader("cseq", "second"));
  assert(std::strcmp(message->headerValue("CSEQ"), "first") == 0);
  for (int index = 2; index < 16; ++index)
    assert(message->addHeader("Repeated", "value"));
  assert(!message->addHeader("Overflow", "ignored"));
  assert(message->headerValue("Overflow") == nullptr);
  msg_free(&message);
}

static void checkBinaryResponseFraming() {
  int sockets[2];
  assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
  rtsp_conn_info connection{};
  connection.fd = sockets[0];
  rtsp_message *response = msg_init();
  response->respondWith(200);
  assert(msg_write_response(&connection, response) == 0);
  char received[256];
  auto count = read(sockets[1], received, sizeof(received));
  assert(std::string(received, count) == "RTSP/1.0 200 OK\r\nContent-Length: 0\r\n\r\n");
  response->addHeader("CSeq", "1");
  response->addHeader("CSeq", "2");
  response->replaceBody(std::string_view("A\0B", 3));
  assert(msg_write_response(&connection, response) == 0);
  count = read(sockets[1], received, sizeof(received));
  std::string expected = "RTSP/1.0 200 OK\r\nCSeq: 1\r\nCSeq: 2\r\nContent-Length: 3\r\n\r\n";
  expected.append("A\0B", 3);
  assert(std::string(received, count) == expected);
  msg_free(&response);
  close(sockets[0]);
  connection.fd = -1;
  close(sockets[1]);
}

struct PendingRequest {
  rtsp_conn_info connection{};
  rtsp_message *message = nullptr;
};

static void *readPendingRequest(void *argument) {
  auto *pending = static_cast<PendingRequest *>(argument);
  rtsp_read_request(&pending->connection, &pending->message);
  return nullptr;
}

static void checkCancellationReleasesRequest() {
  int sockets[2];
  assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
  const std::string headers = "POST /feedback RTSP/1.0\r\nContent-Length: 3\r\n\r\n";
  assert(write(sockets[1], headers.data(), headers.size()) == static_cast<ssize_t>(headers.size()));
  PendingRequest pending;
  pending.connection.fd = sockets[0];
  pthread_t reader;
  assert(pthread_create(&reader, nullptr, readPendingRequest, &pending) == 0);
  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  int available = 0;
  do {
    assert(ioctl(sockets[0], FIONREAD, &available) == 0);
    assert(std::chrono::steady_clock::now() < deadline);
    std::this_thread::yield();
  } while (available != 0);
  assert(pthread_cancel(reader) == 0);
  void *completion = nullptr;
  assert(pthread_join(reader, &completion) == 0);
  assert(completion == PTHREAD_CANCELED);
  assert(pending.message == nullptr);
  close(sockets[0]);
  pending.connection.fd = -1;
  close(sockets[1]);
}

static void checkLoggingReleasesParsedPlist() {
  const auto previousDebugLevel = debug_level();
  RtspMessage message;
  auto plist = plist_new_dict();
  plist_dict_set_item(plist, "value", plist_new_uint(7));
  replaceBodyWithPlist(message, plist);
  plist_free(plist);
  auto previousReleases = releasedPlists;
  set_debug_level(4);
  _debug_log_rtsp_message(nullptr, __FILE__, __LINE__, 4, "owned plist", &message);
  set_debug_level(0);
  assert(releasedPlists == previousReleases + 1);
  set_debug_level(previousDebugLevel);
}

TEST(RtspMessage, CancellationReleasesPartiallyReadRequest) {
  checkCancellationReleasesRequest();
}

TEST(RtspMessage, LoggingReleasesParsedPlistExactlyOnce) {
  checkLoggingReleasesParsedPlist();
}

TEST(RtspMessage, RequestParsingTruncatesFieldsAndRejectsMalformedLines) {
  checkRequestParsing();
}

TEST(RtspMessage, HeaderLimitPreservesFirstCaseInsensitiveDuplicate) {
  checkHeaderLimitAndDuplicates();
}

TEST(RtspMessage, SocketResponsePreservesDuplicateHeaderOrderAndBinaryBody) {
  checkBinaryResponseFraming();
}

TEST(RtspMessage, OwnedRequestBodyPreservesBorrowedBytesAndSupportsEmptyReplacement) {
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

TEST(RtspMessage, ResponseFramingPreservesDuplicatesAndRejectsOversizedBody) {
  RtspMessage framedResponse;
  framedResponse.respondWith(200);
  assert(framedResponse.responsePacket().value() == "RTSP/1.0 200 OK\r\nContent-Length: 0\r\n\r\n");
  framedResponse.addHeader("CSeq", "1");
  framedResponse.addHeader("CSeq", "2");
  framedResponse.replaceBody(std::string_view("A\0B", 3));
  std::string duplicateHeaders = "RTSP/1.0 200 OK\r\nCSeq: 1\r\nCSeq: 2\r\nContent-Length: 3\r\n\r\n";
  duplicateHeaders.append("A\0B", 3);
  assert(framedResponse.responsePacket().value() == duplicateHeaders);
  framedResponse.replaceBody(std::string(4096, 'x'));
  assert(framedResponse.responsePacket().error() == RtspMessage::FramingError::bodyTooLong);
}

TEST(RtspMessage, MetadataCompletenessAndParameterLinesFollowBodyReplacements) {
  RtspMessage metadata;
  const char invalidMetadata[] = {'m', 'l', 'i', 't', 0, 0, 0, 20};
  metadata.replaceBody(std::string_view(invalidMetadata, sizeof(invalidMetadata)));
  assert(!metadata.containsCompleteMetadata());
  const char validMetadata[] = {'m', 'l', 'i', 't', 0, 0, 0, 8, 'm', 'i', 'n', 'm', 0, 0, 0, 0};
  metadata.replaceBody(std::string_view(validMetadata, sizeof(validMetadata)));
  assert(metadata.containsCompleteMetadata());
  metadata.replaceBody("volume: -15.0\r\nprogress: 0/1/2\r\n");
  assert(metadata.parameterLines() == std::vector<std::string>({"volume: -15.0", "progress: 0/1/2"}));
}
