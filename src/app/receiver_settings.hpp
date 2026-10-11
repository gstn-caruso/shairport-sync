#pragma once
#include "app/settings_types.h"
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

struct config_t;
class ConfigurationReader;
class LegacyConfigLease;

struct ConfigurationDiagnostic {
  enum class Kind { warning, notice, debug };
  Kind kind;
  std::string message;
};

struct DiagnosticsSettings {
  int verbosity = 0;
  int debugger_show_elapsed_time{};
  int debugger_show_relative_time{};
  int debugger_show_file_and_line{};
  int statistics_requested{};
  int logOutputLevel{};
  int disable_resend_requests{};
  double diagnostic_drop_packet_fraction{};
};

struct NetworkSettings {
  std::optional<std::string> realPath;
  int endianness = SS_LITTLE_ENDIAN;
  std::string appName{};
  std::string configfile{};
  std::string service_name{};
  std::optional<std::string> password{};
  std::string regtype{};
  std::string regtype2{};
  std::optional<std::string> interface{};
  unsigned interface_index{};
  int port{};
  int udp_port_base{};
  int udp_port_range{};
  double missing_port_dacp_scan_interval_seconds{};
  std::string model{};
  std::string srcvers{};
  std::string osvers{};
  std::string firmware_version{};
  std::string airplay_device_id{};
  std::array<uint8_t, 6> ap1_prefix{};
  std::array<uint8_t, 6> hw_addr{};
  std::string nqptp_shared_memory_interface_name{};
};

struct AudioSettings {
  std::optional<std::string> defaultChannelLayouts;
  stuffing_type packet_stuffing{};
  playback_mode_type playback_mode{};
  double audio_backend_buffer_desired_length{};
  double audio_backend_buffer_interpolation_threshold_in_seconds{};
  double audio_backend_latency_offset{};
  double audio_decoded_buffer_desired_length{};
  int audio_backend_silent_lead_in_time_auto{};
  double audio_backend_silent_lead_in_time{};
  int buffer_start_fill{};
  uint32_t userSuppliedLatency{};
  uint32_t fixedLatencyOffset{};
  uint32_t minimum_free_buffer_headroom{};
  double resync_threshold{};
  double tolerance{};
  uint32_t format_set{};
  uint32_t rate_set{};
  uint32_t channel_set{};
  std::optional<std::string> pa_server{};
  std::optional<std::string> pa_sink{};
  std::optional<std::string> pa_application_name{};
  uint64_t six_channel_layout{};
  uint64_t eight_channel_layout{};
  int mixdown_enable{};
  uint64_t mixdown_channel_layout{};
  int output_channel_mapping_enable{};
  std::vector<std::string> output_channel_map{};
};

struct ReceiverVolumeSettings {
  int ignore_volume_control{};
  int volume_max_db_set{};
  int volume_max_db{};
  uint32_t volume_range_db{};
  int volume_range_hw_priority{};
  volume_control_profile_type volume_control_profile{};
  double default_airplay_volume{};
};

struct SessionSettings {
  double active_state_timeout{};
  int allow_session_interruption{};
  int timeout{};
  int dont_check_timeout{};
  std::optional<std::string> cmd_start{};
  std::optional<std::string> cmd_stop{};
  std::optional<std::string> cmd_set_volume{};
  std::optional<std::string> cmd_unfixable{};
  std::optional<std::string> cmd_active_start{};
  std::optional<std::string> cmd_active_stop{};
  int cmd_blocking{};
  int cmd_start_returns_output{};
  double resend_control_first_check_time{};
  double resend_control_check_interval_time{};
  double resend_control_last_check_time{};
};

class ReceiverSettings {
public:
  ReceiverSettings(const ReceiverSettings &) = delete;
  ReceiverSettings &operator=(const ReceiverSettings &) = delete;
  ReceiverSettings(ReceiverSettings &&) noexcept;
  ReceiverSettings &operator=(ReceiverSettings &&) noexcept;
  ~ReceiverSettings();
  const DiagnosticsSettings &diagnostics() const { return diagnostics_; }
  const NetworkSettings &network() const { return network_; }
  const AudioSettings &audio() const { return audio_; }
  const ReceiverVolumeSettings &volume() const { return volume_; }
  const SessionSettings &session() const { return session_; }
  const std::vector<ConfigurationDiagnostic> &diagnosticsLog() const { return messages_; }

private:
  friend class ConfigurationReader;
  friend class LegacyConfigLease;
  ReceiverSettings();
  config_t *nativeConfiguration() const;
  struct ParsedConfiguration;
  std::unique_ptr<ParsedConfiguration> parsed_;
  DiagnosticsSettings diagnostics_;
  NetworkSettings network_;
  AudioSettings audio_;
  ReceiverVolumeSettings volume_;
  SessionSettings session_;
  std::vector<ConfigurationDiagnostic> messages_;
};
