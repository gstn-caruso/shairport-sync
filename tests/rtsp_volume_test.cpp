#include "session/session_state.hpp"
#include "protocol/rtsp/rtsp.h"
#include "protocol/rtsp/rtsp_message.hpp"
#include "volume/volume_runtime.hpp"
#include <gtest/gtest.h>
#include <array>
#include <format>
#include <memory>
#include <type_traits>

namespace {
class RtspVolume : public testing::Test {
protected:
  SessionState session;
  AirPlayVolume previousSharedLevel = sharedVolumeLevel.current();
  static constexpr std::array infoFields{
    &shairport_cfg::airplay_psi, &shairport_cfg::airplay_fex, &shairport_cfg::airplay_device_id,
    &shairport_cfg::airplay_pi, &shairport_cfg::service_name, &shairport_cfg::model,
    &shairport_cfg::srcvers
  };
  std::array<char *, infoFields.size()> previousInfoStrings{};
  char infoString[20] = "Volume contract";

  void SetUp() override {
    session.thread = pthread_self();
    sharedVolumeLevel.remember(AirPlayVolume{-24});
    for (size_t index = 0; index < infoFields.size(); ++index) {
      previousInfoStrings[index] = config.*infoFields[index];
      config.*infoFields[index] = infoString;
    }
  }
  void TearDown() override {
    sharedVolumeLevel.remember(previousSharedLevel);
    for (size_t index = 0; index < infoFields.size(); ++index)
      config.*infoFields[index] = previousInfoStrings[index];
  }
  void setVolume(std::string_view text) {
    RtspMessage request, response;
    request.request("SET_PARAMETER");
    request.addHeader("Content-Type", "text/parameters");
    request.replaceBody(std::format("volume: {}\r\n", text));
    rtsp_dispatch_request(&session, &request, &response);
    EXPECT_EQ(response.responseCode(), 200);
  }
  std::string getVolume() {
    RtspMessage request, response;
    request.request("GET_PARAMETER");
    request.replaceBody("volume\r\n");
    rtsp_dispatch_request(&session, &request, &response);
    EXPECT_EQ(response.responseCode(), 200);
    return std::string(response.bodyText());
  }
};

TEST_F(RtspVolume, UnrememberedSessionReportsDefaultSharedVolume) {
  EXPECT_EQ(getVolume(), "\r\nvolume: -24.000000\r\n");
}

TEST_F(RtspVolume, UnrememberedSessionReportsSubsequentSharedVolume) {
  sharedVolumeLevel.remember(AirPlayVolume{-10.25});
  EXPECT_EQ(getVolume(), "\r\nvolume: -10.250000\r\n");
}

TEST_F(RtspVolume, RememberedWireVolumeOverridesLaterSharedChanges) {
  setVolume("-15.1234567");
  sharedVolumeLevel.remember(AirPlayVolume{-5});
  EXPECT_DOUBLE_EQ(suggested_volume(&session), -15.123456954956055);
  EXPECT_EQ(getVolume(), "\r\nvolume: -15.123457\r\n");
}

TEST_F(RtspVolume, MalformedWireVolumeIsAcceptedAsZero) {
  setVolume("not-a-number");
  EXPECT_DOUBLE_EQ(suggested_volume(&session), 0);
  EXPECT_EQ(getVolume(), "\r\nvolume: 0.000000\r\n");
}

TEST_F(RtspVolume, WireVolumeAcceptsLeadingWhitespaceAndTrailingText) {
  setVolume("  -12.5remaining");
  EXPECT_DOUBLE_EQ(suggested_volume(&session), -12.5);
  EXPECT_EQ(getVolume(), "\r\nvolume: -12.500000\r\n");
}

TEST_F(RtspVolume, WireRoundingCanProduceTheExactMuteSentinel) {
  setVolume("-144.000001");
  EXPECT_DOUBLE_EQ(suggested_volume(&session), -144);
  EXPECT_EQ(getVolume(), "\r\nvolume: -144.000000\r\n");
}

TEST_F(RtspVolume, InfoPlistPreservesRememberedWireVolumeWithoutDecimalFormatting) {
  setVolume("-15.1234567");
  const std::unique_ptr<std::remove_pointer_t<plist_t>, decltype(&plist_free)>
      info(generateInfoPlist(&session), &plist_free);
  ASSERT_NE(info, nullptr);
  const auto initialVolume = plist_dict_get_item(info.get(), "initialVolume");
  ASSERT_NE(initialVolume, nullptr);
  double reported = 0;
  plist_get_real_val(initialVolume, &reported);
  EXPECT_DOUBLE_EQ(reported, -15.123456954956055);
}
}
