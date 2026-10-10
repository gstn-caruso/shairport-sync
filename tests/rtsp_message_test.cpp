#include "session/session_state.hpp"
#include "runtime/common.h"
#include "protocol/rtsp/rtsp.h"
#include "protocol/rtsp/rtsp_message.hpp"
#include "platform/utilities/rtsp_message_utilities.h"
#include "platform/utilities/debug.h"
#include <gtest/gtest.h>
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

TEST(RtspMessage, RequestLinePreservesMethodAndPathWhileHeadersDetermineBodyLength) {
  rtsp_message *message = nullptr;
  ASSERT_EQ(readLine(&message, "  OPTIONS  /info  RTSP/1.0"), -1);
  EXPECT_STREQ(message->methodName(), "OPTIONS");
  EXPECT_STREQ(message->requestPath(), "/info");
  EXPECT_EQ(readLine(&message, "Content-Length: 3"), -1);
  EXPECT_EQ(readLine(&message, ""), 3);
  msg_free(&message);
}

TEST(RtspMessage, LongRequestTokensAreTruncatedToWireFieldLimits) {
  rtsp_message *message = nullptr;
  ASSERT_EQ(readLine(&message, std::string(30, 'M') + " /" + std::string(300, 'p') + " HTTP/1.1"), -1);
  EXPECT_EQ(std::string_view(message->methodName()), std::string(15, 'M'));
  EXPECT_EQ(std::string_view(message->requestPath()), "/" + std::string(254, 'p'));
  msg_free(&message);
}

TEST(RtspMessage, UnsupportedProtocolVersionReleasesTheRequest) {
  rtsp_message *message = nullptr;
  EXPECT_EQ(readLine(&message, "OPTIONS /info RTSP/2.0"), 0);
  EXPECT_EQ(message, nullptr);
  msg_free(&message);
}

TEST(RtspMessage, HeaderWithoutSeparatorSpaceReleasesTheRequest) {
  rtsp_message *message = nullptr;
  ASSERT_EQ(readLine(&message, "OPTIONS /info RTSP/1.0"), -1);
  EXPECT_EQ(readLine(&message, "Broken:header"), 0);
  EXPECT_EQ(message, nullptr);
  msg_free(&message);
}

TEST(RtspMessage, HeaderLookupPreservesFirstCaseInsensitiveDuplicate) {
  rtsp_message *message = msg_init();
  EXPECT_TRUE(message->addHeader("CSeq", "first"));
  EXPECT_TRUE(message->addHeader("cseq", "second"));
  EXPECT_STREQ(message->headerValue("CSEQ"), "first");
  msg_free(&message);
}

TEST(RtspMessage, SixteenHeadersFitAndTheFollowingHeaderIsRejected) {
  RtspMessage message;
  for (int index = 0; index < 16; ++index) {
    SCOPED_TRACE(index);
    EXPECT_TRUE(message.addHeader("Repeated", "value"));
  }
  EXPECT_FALSE(message.addHeader("Overflow", "ignored"));
  EXPECT_EQ(message.headerValue("Overflow"), nullptr);
  EXPECT_EQ(message.headers().size(), 16);
}

TEST(RtspMessage, SocketResponsePreservesDuplicateHeaderOrderAndBinaryBody) {
  int sockets[2];
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets), 0);
  rtsp_conn_info connection{};
  connection.fd = sockets[0];
  rtsp_message *response = msg_init();
  response->respondWith(200);
  EXPECT_EQ(msg_write_response(&connection, response), 0);
  char received[256];
  auto count = read(sockets[1], received, sizeof(received));
  ASSERT_GE(count, 0);
  EXPECT_EQ(std::string(received, count), "RTSP/1.0 200 OK\r\nContent-Length: 0\r\n\r\n");
  response->addHeader("CSeq", "1");
  response->addHeader("CSeq", "2");
  response->replaceBody(std::string_view("A\0B", 3));
  EXPECT_EQ(msg_write_response(&connection, response), 0);
  count = read(sockets[1], received, sizeof(received));
  std::string expected = "RTSP/1.0 200 OK\r\nCSeq: 1\r\nCSeq: 2\r\nContent-Length: 3\r\n\r\n";
  expected.append("A\0B", 3);
  ASSERT_GE(count, 0);
  EXPECT_EQ(std::string(received, count), expected);
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

TEST(RtspMessage, CancellationReleasesPartiallyReadRequest) {
  int sockets[2];
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets), 0);
  const std::string headers = "POST /feedback RTSP/1.0\r\nContent-Length: 3\r\n\r\n";
  ASSERT_EQ(write(sockets[1], headers.data(), headers.size()), static_cast<ssize_t>(headers.size()));
  PendingRequest pending;
  pending.connection.fd = sockets[0];
  pthread_t reader;
  ASSERT_EQ(pthread_create(&reader, nullptr, readPendingRequest, &pending), 0);
  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  int available = 0;
  do {
    const auto status = ioctl(sockets[0], FIONREAD, &available);
    EXPECT_EQ(status, 0);
    if (status != 0 || std::chrono::steady_clock::now() >= deadline)
      break;
    std::this_thread::yield();
  } while (available != 0);
  EXPECT_EQ(available, 0) << "Reader did not consume request headers before the deadline";
  EXPECT_EQ(pthread_cancel(reader), 0);
  void *completion = nullptr;
  EXPECT_EQ(pthread_join(reader, &completion), 0);
  EXPECT_EQ(completion, PTHREAD_CANCELED);
  EXPECT_EQ(pending.message, nullptr);
  close(sockets[0]);
  pending.connection.fd = -1;
  close(sockets[1]);
}

TEST(RtspMessage, LoggingReleasesParsedPlistExactlyOnce) {
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
  EXPECT_EQ(releasedPlists, previousReleases + 1);
  set_debug_level(previousDebugLevel);
}

TEST(RtspMessage, OwnedRequestBodyPreservesBorrowedBytesAndSupportsEmptyReplacement) {
  RtspMessage owned;
  EXPECT_EQ(owned.readLine("OPTIONS /info RTSP/1.0"), -1);
  EXPECT_TRUE(owned.requestsMethod("OPTIONS"));
  EXPECT_TRUE(owned.requestsPath("/info"));
  EXPECT_EQ(owned.readLine("Content-Length: 3"), -1);
  EXPECT_EQ(owned.readLine(""), 3);
  std::string borrowed("A\0B", 3);
  owned.replaceBody(borrowed);
  borrowed[0] = 'X';
  EXPECT_EQ(owned.bodyText(), std::string_view("A\0B", 3));
  EXPECT_EQ(owned.bodyData()[3], '\0');
  owned.replaceBody("");
  EXPECT_EQ(owned.bodyLength(), 0);
}

TEST(RtspMessage, EmptyResponseContainsAZeroContentLength) {
  RtspMessage framedResponse;
  framedResponse.respondWith(200);
  auto packet = framedResponse.responsePacket();
  ASSERT_TRUE(packet.has_value());
  EXPECT_EQ(*packet, "RTSP/1.0 200 OK\r\nContent-Length: 0\r\n\r\n");
}

TEST(RtspMessage, ResponsePacketPreservesDuplicateHeadersAndBinaryBody) {
  RtspMessage framedResponse;
  framedResponse.respondWith(200);
  framedResponse.addHeader("CSeq", "1");
  framedResponse.addHeader("CSeq", "2");
  framedResponse.replaceBody(std::string_view("A\0B", 3));
  std::string duplicateHeaders = "RTSP/1.0 200 OK\r\nCSeq: 1\r\nCSeq: 2\r\nContent-Length: 3\r\n\r\n";
  duplicateHeaders.append("A\0B", 3);
  auto packet = framedResponse.responsePacket();
  ASSERT_TRUE(packet.has_value());
  EXPECT_EQ(*packet, duplicateHeaders);
}

TEST(RtspMessage, OversizedResponseBodyReturnsAFramingError) {
  RtspMessage framedResponse;
  framedResponse.respondWith(200);
  framedResponse.replaceBody(std::string(4096, 'x'));
  auto packet = framedResponse.responsePacket();
  ASSERT_FALSE(packet.has_value());
  EXPECT_EQ(packet.error(), RtspMessage::FramingError::bodyTooLong);
}

TEST(RtspMessage, CompleteMetadataReplacesAnIncompleteContainer) {
  RtspMessage metadata;
  const char invalidMetadata[] = {'m', 'l', 'i', 't', 0, 0, 0, 20};
  metadata.replaceBody(std::string_view(invalidMetadata, sizeof(invalidMetadata)));
  EXPECT_FALSE(metadata.containsCompleteMetadata());
  const char validMetadata[] = {'m', 'l', 'i', 't', 0, 0, 0, 8, 'm', 'i', 'n', 'm', 0, 0, 0, 0};
  metadata.replaceBody(std::string_view(validMetadata, sizeof(validMetadata)));
  EXPECT_TRUE(metadata.containsCompleteMetadata());
}

TEST(RtspMessage, ParameterLinesPreserveVolumeAndProgressValues) {
  RtspMessage metadata;
  metadata.replaceBody("volume: -15.0\r\nprogress: 0/1/2\r\n");
  EXPECT_EQ(metadata.parameterLines(), std::vector<std::string>({"volume: -15.0", "progress: 0/1/2"}));
}
