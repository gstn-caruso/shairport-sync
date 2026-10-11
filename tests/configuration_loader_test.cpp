#include "app/configuration_loader.hpp"
#include "runtime/common.h"
#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <unistd.h>

namespace {
class ConfigurationLoaderTest : public testing::Test {
protected:
  std::filesystem::path file = std::filesystem::temp_directory_path() /
      ("receiver-settings-" + std::to_string(getpid()) + ".conf");
  void TearDown() override { std::filesystem::remove(file); }
  auto load(std::string_view contents) {
    std::ofstream(file) << contents;
    const auto path = file.string();
    const std::array<std::string_view, 3> arguments{"--check-config", "--config", path};
    return ConfigurationLoader::load(*StartupOptions::parse(arguments));
  }
};

TEST_F(ConfigurationLoaderTest, DefaultsPreserveNetworkAudioVolumeAndSessionValues) {
  auto settings = load("");
  ASSERT_TRUE(settings);
  EXPECT_EQ(config.port, 7000);
  EXPECT_EQ(config.udp_port_base, 6001);
  EXPECT_EQ(config.udp_port_range, 10);
  EXPECT_DOUBLE_EQ(config.audio_backend_buffer_desired_length, 0.35);
  EXPECT_DOUBLE_EQ(config.audio_decoded_buffer_desired_length, 0.75);
  EXPECT_DOUBLE_EQ(config.audio_backend_buffer_interpolation_threshold_in_seconds, 0.02);
  EXPECT_DOUBLE_EQ(config.default_airplay_volume, -24);
  EXPECT_EQ(config.timeout, 60);
  EXPECT_DOUBLE_EQ(config.active_state_timeout, 10);
  EXPECT_EQ(config.output_channel_mapping_enable, 1);
  EXPECT_EQ(config.mixdown_enable, 1);
  EXPECT_STREQ(config.airplay_device_id, "00:00:00:00:00:00");
}

TEST_F(ConfigurationLoaderTest, FullConfigurationPreservesAcceptedSettingsAndOwnedStrings) {
  auto settings = load(R"(
    general = {
      name = "Living Room"; port = 7100; udp_port_base = 6100; udp_port_range = 12;
      interpolation = "basic"; playback_mode = "reverse stereo";
      drift_tolerance_in_seconds = 0.004; resync_threshold_in_seconds = 0.1;
      ignore_volume_control = "yes"; volume_max_db = -3.8; volume_range_db = 40;
      volume_control_profile = "flat"; volume_control_combined_hardware_priority = "yes";
      default_airplay_volume = -12.5; password = "test credential";
      airplay_device_id = 0x112233445566L; airplay_device_id_offset = 1;
      run_this_when_volume_is_set = "volume-hook";
      audio_backend_buffer_desired_length_in_seconds = 0.4;
      audio_decoded_buffer_desired_length_in_seconds = 0.9;
      audio_backend_latency_offset_in_seconds = -0.02;
      audio_backend_silent_lead_in_time = 0.5;
      output_channel_mapping = ("FR", "FL"); six_channel_mode = "off"; mixdown = "stereo";
    };
    diagnostics = { statistics = "yes"; log_verbosity = 1; log_show_file_and_line = "no"; };
    pulseaudio = { server = "native-server"; sink = "native-sink"; application_name = "Receiver";
      output_format = ("S16_LE", "S32_LE"); output_rate = (44100, 48000); output_channels = (2, 6); };
    sessioncontrol = { run_this_before_play_begins = "begin-hook"; run_this_after_play_ends = "end-hook";
      wait_for_completion = "yes"; before_play_begins_returns_output = "yes";
      session_timeout = 120; active_state_timeout = 5.5; allow_session_interruption = "yes"; };
  )");
  ASSERT_TRUE(settings);
  std::filesystem::remove(file);
  EXPECT_STREQ(config.service_name, "Living Room");
  EXPECT_EQ(config.port, 7100);
  EXPECT_EQ(config.udp_port_base, 6100);
  EXPECT_EQ(config.udp_port_range, 12);
  EXPECT_EQ(config.packet_stuffing, ST_basic);
  EXPECT_EQ(config.playback_mode, ST_reverse_stereo);
  EXPECT_EQ(config.volume_max_db, -3);
  EXPECT_EQ(config.volume_range_db, 40u);
  EXPECT_DOUBLE_EQ(config.default_airplay_volume, -12.5);
  EXPECT_STREQ(config.airplay_device_id, "11:22:33:44:55:67");
  EXPECT_STREQ(config.pa_server, "native-server");
  EXPECT_STREQ(config.pa_sink, "native-sink");
  EXPECT_STREQ(config.cmd_start, "begin-hook");
  EXPECT_EQ(config.cmd_blocking, 1);
  EXPECT_EQ(config.cmd_start_returns_output, 1);
  EXPECT_EQ(config.timeout, 120);
  EXPECT_DOUBLE_EQ(config.audio_backend_latency_offset, -0.02);
  EXPECT_EQ(config.audio_backend_silent_lead_in_time_auto, 0);
  EXPECT_STREQ(config.output_channel_map[0], "FR");
  EXPECT_STREQ(config.output_channel_map[1], "FL");
  EXPECT_EQ(config.six_channel_layout, 0u);
}
}
