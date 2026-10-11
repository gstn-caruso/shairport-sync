#include "app/configuration_loader.hpp"
#include <array>
#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <unistd.h>
#include <sys/wait.h>
extern "C" {
#include <libavutil/log.h>
}

namespace {
class ConfigurationLoaderTest : public testing::Test {
protected:
  std::filesystem::path file = std::filesystem::temp_directory_path() /
      ("receiver-settings-" + std::to_string(getpid()) + ".conf");
  void TearDown() override { std::filesystem::remove(file); }
  ConfigurationEnvironment environment{.defaultPath = file.string(), .hostname = "receiver.local",
      .packageVersion = "1.2.3", .detailedVersion = "1.2.3-test", .firmwareVersion = "test-firmware"};
  auto load(std::string_view contents) {
    std::ofstream(file) << contents;
    const auto path = file.string();
    const std::array<std::string_view, 3> arguments{"--check-config", "--config", path};
    return ConfigurationLoader::load(*StartupOptions::parse(arguments), environment);
  }
};

TEST_F(ConfigurationLoaderTest, DefaultsPreserveNetworkAudioVolumeAndSessionValues) {
  auto settings = load("");
  ASSERT_TRUE(settings);
  EXPECT_EQ(settings->network().port, 7000);
  EXPECT_EQ(settings->network().udp_port_base, 6001);
  EXPECT_EQ(settings->network().udp_port_range, 10);
  EXPECT_DOUBLE_EQ(settings->audio().audio_backend_buffer_desired_length, 0.35);
  EXPECT_DOUBLE_EQ(settings->audio().audio_decoded_buffer_desired_length, 0.75);
  EXPECT_DOUBLE_EQ(settings->audio().audio_backend_buffer_interpolation_threshold_in_seconds, 0.02);
  EXPECT_DOUBLE_EQ(settings->volume().default_airplay_volume, -24);
  EXPECT_EQ(settings->session().timeout, 60);
  EXPECT_DOUBLE_EQ(settings->session().active_state_timeout, 10);
  EXPECT_EQ(settings->audio().output_channel_mapping_enable, 1);
  EXPECT_EQ(settings->audio().mixdown_enable, 1);
  EXPECT_STREQ(settings->network().airplay_device_id.c_str(), "00:00:00:00:00:00");
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
  EXPECT_STREQ(settings->network().service_name.c_str(), "Living Room");
  EXPECT_EQ(settings->network().port, 7100);
  EXPECT_EQ(settings->network().udp_port_base, 6100);
  EXPECT_EQ(settings->network().udp_port_range, 12);
  EXPECT_EQ(settings->audio().packet_stuffing, ST_basic);
  EXPECT_EQ(settings->audio().playback_mode, ST_reverse_stereo);
  EXPECT_EQ(settings->volume().volume_max_db, -3);
  EXPECT_EQ(settings->volume().volume_range_db, 40u);
  EXPECT_DOUBLE_EQ(settings->volume().default_airplay_volume, -12.5);
  EXPECT_STREQ(settings->network().airplay_device_id.c_str(), "11:22:33:44:55:67");
  EXPECT_STREQ(settings->audio().pa_server->c_str(), "native-server");
  EXPECT_STREQ(settings->audio().pa_sink->c_str(), "native-sink");
  EXPECT_STREQ(settings->session().cmd_start->c_str(), "begin-hook");
  EXPECT_EQ(settings->session().cmd_blocking, 1);
  EXPECT_EQ(settings->session().cmd_start_returns_output, 1);
  EXPECT_EQ(settings->session().timeout, 120);
  EXPECT_DOUBLE_EQ(settings->audio().audio_backend_latency_offset, -0.02);
  EXPECT_EQ(settings->audio().audio_backend_silent_lead_in_time_auto, 0);
  EXPECT_STREQ(settings->audio().output_channel_map[0].c_str(), "FR");
  EXPECT_STREQ(settings->audio().output_channel_map[1].c_str(), "FL");
  EXPECT_EQ(settings->audio().six_channel_layout, 0u);
}

TEST_F(ConfigurationLoaderTest, MovedSettingsAndTwoConfigurationsRetainIndependentOwnedValues) {
  auto first = load(R"(general = { name = "First"; output_channel_mapping = ("FL", "FR"); };
                        pulseaudio = { server = "first-server"; };)");
  ASSERT_TRUE(first);
  auto second = load(R"(general = { name = "Second"; port = 7200; };
                         sessioncontrol = { run_this_before_play_begins = "second-hook"; };)");
  ASSERT_TRUE(second);
  std::filesystem::remove(file);
  ReceiverSettings moved = std::move(*first);
  EXPECT_EQ(moved.network().service_name, "First");
  EXPECT_EQ(moved.audio().pa_server, "first-server");
  EXPECT_EQ(moved.audio().output_channel_map, (std::vector<std::string>{"FL", "FR"}));
  EXPECT_EQ(second->network().service_name, "Second");
  EXPECT_EQ(second->network().port, 7200);
  EXPECT_EQ(second->session().cmd_start, "second-hook");
}

TEST_F(ConfigurationLoaderTest, ValidMalformedValidLoadsHaveNoStaleStateOrNativeLogEffects) {
  const auto previous = av_log_get_level();
  av_log_set_level(AV_LOG_QUIET);
  auto first = load(R"(general = { name = "First"; port = 7200; };)");
  EXPECT_TRUE(first);
  auto malformed = load("general = {");
  EXPECT_FALSE(malformed);
  auto last = load("");
  EXPECT_TRUE(last);
  EXPECT_EQ(av_log_get_level(), AV_LOG_QUIET);
  av_log_set_level(previous);
  ASSERT_TRUE(last);
  EXPECT_EQ(last->network().port, 7000);
  EXPECT_EQ(last->network().service_name, "Receiver");
}

TEST_F(ConfigurationLoaderTest, MissingExplicitFileFailsWhileMissingDefaultUsesDefaults) {
  std::filesystem::remove(file);
  const auto path = file.string();
  const std::array<std::string_view, 2> explicitArguments{"--config", path};
  auto missing = ConfigurationLoader::load(*StartupOptions::parse(explicitArguments), environment);
  ASSERT_FALSE(missing);
  EXPECT_NE(missing.error().message.find("Unable to read configuration"), std::string::npos);
  auto defaults = ConfigurationLoader::load(*StartupOptions::parse({}), environment);
  ASSERT_TRUE(defaults);
  EXPECT_FALSE(defaults->network().realPath);
  EXPECT_EQ(defaults->network().port, 7000);
}

TEST_F(ConfigurationLoaderTest, UnreadableExplicitAndDefaultFilesAreBothRejected) {
  std::ofstream(file) << "general = { port = 7100; };";
  std::filesystem::permissions(file, std::filesystem::perms::none);
  const auto path = file.string();
  const std::array<std::string_view, 2> arguments{"--config", path};
  const auto explicitlyRequested = *StartupOptions::parse(arguments);
  const auto defaultRequested = *StartupOptions::parse({});
  const auto rejected = [&] {
    return !ConfigurationLoader::load(explicitlyRequested, environment) &&
           !ConfigurationLoader::load(defaultRequested, environment);
  };
  if (geteuid() != 0) {
    EXPECT_TRUE(rejected());
    return;
  }
  const auto child = fork();
  ASSERT_GE(child, 0);
  if (child == 0) {
    if (setgid(65534) != 0 || setuid(65534) != 0) _exit(2);
    _exit(rejected() ? 0 : 1);
  }
  int status = 0;
  ASSERT_EQ(waitpid(child, &status, 0), child);
  ASSERT_TRUE(WIFEXITED(status));
  EXPECT_EQ(WEXITSTATUS(status), 0);
}

TEST_F(ConfigurationLoaderTest, FatalValidationRetainsEarlierWarningsInTheirOriginalOrder) {
  auto invalid = load(R"(general = { statistics = "yes"; drift = 1; };
                          diagnostics = { log_verbosity = 9; };)");
  ASSERT_FALSE(invalid);
  EXPECT_NE(invalid.error().message.find("Invalid diagnostics log_verbosity"), std::string::npos);
  ASSERT_GE(invalid.error().precedingDiagnostics.size(), 2u);
  EXPECT_NE(invalid.error().precedingDiagnostics[0].message.find("statistics"), std::string::npos);
  EXPECT_NE(invalid.error().precedingDiagnostics[1].message.find("drift setting"), std::string::npos);
  EXPECT_TRUE(load(""));
}

TEST_F(ConfigurationLoaderTest, ExplicitEnvironmentDeterminesIdentityNameAndInterfaceResolution) {
  environment.hardwareAddress = {0, 1, 2, 3, 4, 5};
  environment.interfaceIndex = [](std::string_view name) { return name == "test0" ? 7u : 0u; };
  auto settings = load(R"(general = { name = "%h %v %V"; interface = "test0";
                                     airplay_device_id_offset = 2; };)");
  ASSERT_TRUE(settings);
  EXPECT_EQ(settings->network().service_name, "receiver 1.2.3 1.2.3-test");
  EXPECT_EQ(settings->network().airplay_device_id, "00:01:02:03:04:07");
  EXPECT_EQ(settings->network().interface, "test0");
  EXPECT_EQ(settings->network().interface_index, 7u);
  auto absent = load(R"(general = { interface = "missing"; };)");
  ASSERT_TRUE(absent);
  EXPECT_FALSE(absent->network().interface);
  ASSERT_FALSE(absent->diagnosticsLog().empty());
  EXPECT_NE(absent->diagnosticsLog()[0].message.find("was not found"), std::string::npos);
}

TEST_F(ConfigurationLoaderTest, InvalidLayoutsChannelsAndVolumeRetainDefaultsWithOrderedWarnings) {
  auto settings = load(R"(general = { default_airplay_volume = 1; six_channel_mode = "stereo";
      eight_channel_mode = "unknown-layout"; mixdown = "not-a-layout";
      output_channel_mapping = ("unknown-channel", "FR"); };
      pulseaudio = { output_channels = (0, 32); output_rate = (9999);
                    output_format = "unavailable"; };)");
  ASSERT_TRUE(settings);
  EXPECT_DOUBLE_EQ(settings->volume().default_airplay_volume, -24);
  EXPECT_EQ(settings->audio().channel_set, SPS_CHANNEL_SET);
  EXPECT_EQ(settings->audio().rate_set, SPS_RATE_SET);
  EXPECT_EQ(settings->audio().format_set, SPS_FORMAT_SET);
  EXPECT_EQ(settings->audio().output_channel_map, (std::vector<std::string>{"--", "FR"}));
  EXPECT_EQ(settings->audio().mixdown_enable, 1);
  EXPECT_EQ(settings->audio().mixdown_channel_layout, 0u);
  EXPECT_GT(settings->audio().six_channel_layout, 0u);
  EXPECT_GT(settings->audio().eight_channel_layout, 0u);
  EXPECT_GE(settings->diagnosticsLog().size(), 7u);
}

TEST_F(ConfigurationLoaderTest, RemovedOptionsNumericTypesAndRangeFailuresRemainRejected) {
  for (const auto text : {"alsa = {};", "general = { port = 70000; };",
                         "general = { name = 10; };", "general = { port = 1e40; };",
                         "general = { output_channel_mapping = (\"FL\", 1); };",
                         "latencies = { default = 1; };"}) {
    SCOPED_TRACE(text);
    EXPECT_FALSE(load(text));
  }
  auto converted = load("general = { port = 7100.9; volume_max_db = -3.8; };");
  ASSERT_TRUE(converted);
  EXPECT_EQ(converted->network().port, 7100);
  EXPECT_EQ(converted->volume().volume_max_db, -3);
}
}
