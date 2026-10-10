#include "session/session_state.hpp"
#include "runtime/common.h"
#include "protocol/rtsp/rtsp.h"
#include "protocol/rtsp/rtsp_message.hpp"
#include "platform/utilities/rtsp_message_utilities.h"
#include <gtest/gtest.h>

namespace {
class RtspDispatch : public testing::Test {
protected:
  rtsp_conn_info connection{};
  RtspMessage request;
  RtspMessage response;

  void SetUp() override { connection.thread = pthread_self(); }
  void dispatch() { rtsp_dispatch_request(&connection, &request, &response); }
};

TEST_F(RtspDispatch, SettingVolumeAndProgressRemembersTheRequestedLevel) {
  request.request("SET_PARAMETER");
  request.addHeader("Content-Type", "text/parameters");
  request.replaceBody("volume: -15.000000\r\nprogress: 0/44100/88200\r\n");

  dispatch();

  EXPECT_EQ(response.responseCode(), 200);
  EXPECT_DOUBLE_EQ(suggested_volume(&connection), -15.0);
}

TEST_F(RtspDispatch, GettingVolumeReturnsTheSessionLevelWithSixDecimalPlaces) {
  connection.volumeControl.rememberLevel(-15.0);
  request.request("GET_PARAMETER");
  request.replaceBody("volume\r\n");

  dispatch();

  EXPECT_EQ(response.responseCode(), 200);
  EXPECT_EQ(response.bodyText(), "\r\nvolume: -15.000000\r\n");
}

struct MethodResponse {
  const char *method;
  int status;
};

void PrintTo(const MethodResponse &method, std::ostream *output) {
  *output << method.method << " -> " << method.status;
}

class RtspMethodResponse : public RtspDispatch,
                           public testing::WithParamInterface<MethodResponse> {};

TEST_P(RtspMethodResponse, RespondsWithoutPriorRequests) {
  request.request(GetParam().method, "/unsupported");

  dispatch();

  EXPECT_EQ(response.responseCode(), GetParam().status);
}

INSTANTIATE_TEST_SUITE_P(Methods, RtspMethodResponse, testing::Values(
  MethodResponse{"RECORD", 200}, MethodResponse{"TEARDOWN", 200},
  MethodResponse{"GET", 501}, MethodResponse{"POST", 501}, MethodResponse{"FLUSH", 451},
  MethodResponse{"ANNOUNCE", 501}, MethodResponse{"PAUSE", 501},
  MethodResponse{"UNSUPPORTED", 501}
), [](const auto &info) { return info.param.method; });

TEST_F(RtspDispatch, OptionsAdvertisesAirplay2MethodsWithoutLegacyAnnounce) {
  request.request("OPTIONS");

  dispatch();

  EXPECT_EQ(response.responseCode(), 200);
  const char *methods = response.headerValue("Public");
  ASSERT_NE(methods, nullptr);
  const std::string_view advertised(methods);
  EXPECT_EQ(advertised.find("ANNOUNCE"), std::string_view::npos);
  EXPECT_NE(advertised.find("FLUSHBUFFERED"), std::string_view::npos);
  EXPECT_NE(advertised.find("GET_PARAMETER"), std::string_view::npos);
}

TEST_F(RtspDispatch, SetupRejectsNtpTiming) {
  request.request("SETUP");
  plist_t setup = plist_new_dict();
  plist_dict_set_item(setup, "timingProtocol", plist_new_string("NTP"));
  replaceBodyWithPlist(request, setup);
  plist_free(setup);

  dispatch();

  EXPECT_EQ(response.responseCode(), 400);
}

TEST_F(RtspDispatch, MetadataRejectsAnIncompleteContainer) {
  request.request("SET_PARAMETER");
  request.addHeader("Content-Type", "application/x-dmap-tagged");
  const char metadata[] = {'m', 'l', 'i', 't', 0, 0, 0, 20};
  request.replaceBody({metadata, sizeof(metadata)});

  dispatch();

  EXPECT_EQ(response.responseCode(), 400);
}

TEST_F(RtspDispatch, MetadataAcceptsACompleteContainer) {
  request.request("SET_PARAMETER");
  request.addHeader("Content-Type", "application/x-dmap-tagged");
  const char metadata[] = {'m', 'l', 'i', 't', 0, 0, 0, 8, 'm', 'i', 'n', 'm', 0, 0, 0, 0};
  request.replaceBody({metadata, sizeof(metadata)});

  dispatch();

  EXPECT_EQ(response.responseCode(), 200);
}

TEST_F(RtspDispatch, CompleteMetadataIsAcceptedAfterAnIncompleteRequest) {
  request.request("SET_PARAMETER");
  request.addHeader("Content-Type", "application/x-dmap-tagged");
  const char incomplete[] = {'m', 'l', 'i', 't', 0, 0, 0, 20};
  request.replaceBody({incomplete, sizeof(incomplete)});
  dispatch();
  ASSERT_EQ(response.responseCode(), 400);
  const char complete[] = {'m', 'l', 'i', 't', 0, 0, 0, 8, 'm', 'i', 'n', 'm', 0, 0, 0, 0};
  request.replaceBody({complete, sizeof(complete)});
  response = RtspMessage{};

  dispatch();

  EXPECT_EQ(response.responseCode(), 200);
}
}
