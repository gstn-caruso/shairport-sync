#include "session/session_state.hpp"
#include "protocol/rtsp/rtsp.h"
#include "protocol/rtsp/rtsp_message.hpp"
#include "platform/utilities/rtsp_message_utilities.h"
#include <gtest/gtest.h>
#include <optional>

class BufferedFlushDispatch : public testing::Test {
protected:
  void SetUp() override {
    connection.thread = pthread_self();
    ASSERT_EQ(pthread_mutex_init(&connection.flush_mutex, nullptr), 0);
    connection.ap2_play_enabled = 1;
    connection.clock.setAnchor(1, 10, 100, 100);
    ASSERT_TRUE(connection.clock.hasAnchor());
  }
  void TearDown() override { pthread_mutex_destroy(&connection.flush_mutex); }
  void flush(std::optional<uint64_t> from, uint64_t until) {
    RtspMessage request, response;
    request.request("FLUSHBUFFERED");
    auto plist = plist_new_dict();
    if (from) {
      plist_dict_set_item(plist, "flushFromSeq", plist_new_uint(*from));
      plist_dict_set_item(plist, "flushFromTS", plist_new_uint(100));
    }
    plist_dict_set_item(plist, "flushUntilSeq", plist_new_uint(until));
    plist_dict_set_item(plist, "flushUntilTS", plist_new_uint(200));
    replaceBodyWithPlist(request, plist);
    plist_free(plist);
    rtsp_dispatch_request(&connection, &request, &response);
    EXPECT_EQ(response.responseCode(), 200);
  }
  SessionState connection{};
};

TEST_F(BufferedFlushDispatch, ImmediateRequestPausesPlaybackAndClearsClockAnchor) {
  flush(std::nullopt, 20);
  EXPECT_EQ(connection.ap2_play_enabled, 0);
  EXPECT_FALSE(connection.clock.hasAnchor());
}

TEST_F(BufferedFlushDispatch, DeferredRequestPreservesPlaybackAndClockAnchor) {
  flush(10, 20);
  EXPECT_EQ(connection.ap2_play_enabled, 1);
  EXPECT_TRUE(connection.clock.hasAnchor());
}

TEST_F(BufferedFlushDispatch, FullDeferredQueueStillAcknowledgesEleventhRequest) {
  for (uint64_t index = 0; index < 11; ++index)
    flush(100 + index * 10, 105 + index * 10);
  EXPECT_EQ(connection.ap2_play_enabled, 1);
  EXPECT_TRUE(connection.clock.hasAnchor());
}
