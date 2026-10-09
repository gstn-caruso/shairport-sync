#include "rtp_clock.hpp"
#include <gtest/gtest.h>

TEST(RtpClock, MissingAnchorPreventsConversions) {
  RtpClock clock;
  EXPECT_EQ(clock.status(), clock_no_anchor_info);
  EXPECT_EQ(clock.observe({clock_ok, 7, 1000000000, 100, 0}, 1000000000), clock_no_anchor_info);
  EXPECT_FALSE(clock.anchorFrame(44100, 0.0));
  EXPECT_FALSE(clock.localTimeForFrame(100, 44100, 0.0));
  EXPECT_FALSE(clock.frameForLocalTime(1000000000, 44100, 0.0));
}

TEST(RtpClock, MastershipValidityIncludesExactBoundary) {
  RtpClock clock;
  clock.setAnchor(7, 100, 9000000000, 1000000000);
  EXPECT_EQ(clock.observe({clock_ok, 7, 1000000000, 100000000, 600000001}, 1000000000), clock_not_valid);
  EXPECT_EQ(clock.observe({clock_ok, 7, 1000000000, 100000000, 800000000}, 1000000000), clock_not_valid);
  EXPECT_FALSE(clock.localTimeForFrame(100, 44100, 0.0));
  EXPECT_EQ(clock.observe({clock_ok, 7, 1000000000, 100000000, 600000000}, 1000000000), clock_ok);
}

TEST(RtpClock, ValidAnchorConvertsFramesAndTimesWithLatency) {
  RtpClock clock;
  clock.setAnchor(7, 100, 9000000000, 1000000000);
  ASSERT_EQ(clock.observe({clock_ok, 7, 1000000000, 100000000, 600000000}, 1000000000), clock_ok);
  EXPECT_EQ(clock.anchorFrame(44100, 0.0), 100);
  EXPECT_EQ(clock.anchorFrame(44100, 0.1), uint32_t(100 - 4410));
  EXPECT_EQ(clock.localTimeForFrame(44200, 44100, 0.0), 9900000000);
  EXPECT_EQ(clock.frameForLocalTime(9900000000, 44100, 0.0), 44200);
  EXPECT_EQ(clock.localTimeForFrame(100, 48000, 0.1), 9000000000);
  EXPECT_FALSE(clock.localTimeForFrame(100, 0, 0.0));
  EXPECT_FALSE(clock.frameForLocalTime(1000000000, 0, 0.0));
}

TEST(RtpClock, FrameWraparoundPreservesConversionRounding) {
  RtpClock clock;
  clock.setAnchor(7, UINT32_MAX - 10, 9000000000, 7000000000);
  clock.observe({clock_ok, 7, 7000000000, 100000000, 600000000}, 7000000000);
  EXPECT_EQ(clock.localTimeForFrame(9, 44100, 0.0), 8900453514);
  EXPECT_EQ(clock.frameForLocalTime(8900453514, 44100, 0.0), 8);
}

TEST(RtpClock, ChangedMasterUsesFallbackThenNewClockOffset) {
  RtpClock fallback;
  fallback.setAnchor(7, 100, 9000000000, 1000000000);
  fallback.observe({clock_ok, 7, 1000000000, 100000000, 0}, 1000000000);
  EXPECT_EQ(fallback.observe({clock_ok, 8, 4000000000, 300000000, 1000000000}, 4000000000), clock_ok);
  EXPECT_EQ(fallback.localTimeForFrame(100, 44100, 0.0), 8900000000);
  EXPECT_EQ(fallback.observe({clock_ok, 8, 6000000001, 300000000, 5000000000}, 6000000001), clock_ok);
  fallback.observe({clock_ok, 8, 6100000000, 400000000, 5000000000}, 6100000000);
  EXPECT_EQ(fallback.localTimeForFrame(100, 44100, 0.0), 8800000000);
}

TEST(RtpClock, UnreadyObservationKeepsAnchorAndResetClearsIt) {
  RtpClock fallback;
  fallback.setAnchor(7, 100, 9000000000, 1000000000);
  fallback.observe({clock_ok, 7, 1000000000, 100000000, 0}, 1000000000);
  fallback.observe({clock_ok, 8, 4000000000, 300000000, 1000000000}, 4000000000);
  fallback.observe({clock_ok, 8, 6000000001, 300000000, 5000000000}, 6000000001);
  fallback.observe({clock_ok, 8, 6100000000, 400000000, 5000000000}, 6100000000);
  EXPECT_EQ(fallback.observe({clock_not_ready, 0, 0, 0, 0}, 6200000000), clock_not_ready);
  EXPECT_EQ(fallback.anchorFrame(44100, 0.0), 100);
  EXPECT_FALSE(fallback.localTimeForFrame(100, 44100, 0.0));
  fallback.reset();
  EXPECT_EQ(fallback.status(), clock_no_anchor_info);
  EXPECT_FALSE(fallback.anchorFrame(44100, 0.0));
}

TEST(RtpClock, ReplacingAnchorInvalidatesOldTimingInformation) {
  RtpClock changed;
  changed.setAnchor(7, 100, 9000000000, 1000000000);
  changed.observe({clock_ok, 7, 1000000000, 100000000, 0}, 1000000000);
  changed.setAnchor(7, 500, 9100000000, 2000000000);
  EXPECT_FALSE(changed.anchorFrame(44100, 0.0));
  EXPECT_EQ(changed.observe({clock_ok, 8, 2000000000, 300000000, 0}, 2000000000), clock_not_valid);
}

TEST(RtpClock, MastershipWindowBoundaryRetainsFallbackOffset) {
  RtpClock boundary;
  boundary.setAnchor(7, 100, 9000000000, 1000000000);
  boundary.observe({clock_ok, 7, 1000000000, 100000000, 0}, 1000000000);
  boundary.observe({clock_ok, 8, 6000000000, 300000000, 5000000000}, 6000000000);
  boundary.observe({clock_ok, 7, 6100000000, 200000000, 0}, 6100000000);
  EXPECT_EQ(boundary.localTimeForFrame(100, 44100, 0.0), 8800000000);
}
