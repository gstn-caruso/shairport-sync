#include "app/configuration_validation.hpp"
#include <array>
#include <string_view>

std::expected<void, std::string> validateConfigurationTypes(const config_t &settings) {
  for (const char *group : {"general", "pulseaudio", "sessioncontrol", "diagnostics", "latencies"}) {
    if (const auto *value = config_lookup(&settings, group);
        value && config_setting_type(value) != CONFIG_TYPE_GROUP)
      return std::unexpected(std::string(group) + " must be a settings group");
  }
  constexpr std::array stringSettings{
    "diagnostics.disable_resend_requests",
    "diagnostics.log_output_level",
    "diagnostics.log_output_to",
    "diagnostics.log_show_file_and_line",
    "diagnostics.log_show_time_since_last_message",
    "diagnostics.log_show_time_since_startup",
    "diagnostics.statistics",
    "general.eight_channel_mode",
    "general.ignore_volume_control",
    "general.interface",
    "general.interpolation",
    "general.mixdown",
    "general.name",
    "general.password",
    "general.playback_mode",
    "general.regtype",
    "general.run_this_when_volume_is_set",
    "general.service_type",
    "general.six_channel_mode",
    "general.statistics",
    "general.volume_control_combined_hardware_priority",
    "general.volume_control_profile",
    "pulseaudio.application_name",
    "pulseaudio.default_channel_layouts",
    "pulseaudio.server",
    "pulseaudio.sink",
    "sessioncontrol.allow_session_interruption",
    "sessioncontrol.before_play_begins_returns_output",
    "sessioncontrol.run_this_after_play_ends",
    "sessioncontrol.run_this_after_exiting_active_state",
    "sessioncontrol.run_this_before_play_begins",
    "sessioncontrol.run_this_before_entering_active_state",
    "sessioncontrol.run_this_if_an_unfixable_error_is_detected",
    "sessioncontrol.wait_for_completion"
  };
  constexpr std::array numericSettings{
    "diagnostics.drop_this_fraction_of_audio_packets",
    "diagnostics.log_verbosity",
    "general.airplay_device_id",
    "general.airplay_device_id_offset",
    "general.audio_backend_buffer_desired_length",
    "general.audio_backend_buffer_desired_length_in_seconds",
    "general.audio_backend_buffer_interpolation_threshold_in_seconds",
    "general.audio_backend_latency_offset",
    "general.audio_backend_latency_offset_in_seconds",
    "general.audio_decoded_buffer_desired_length_in_seconds",
    "general.default_airplay_volume",
    "general.drift",
    "general.drift_tolerance_in_seconds",
    "general.log_verbosity",
    "general.missing_port_dacp_scan_interval_seconds",
    "general.port",
    "general.resend_control_check_interval_time",
    "general.resend_control_first_check_time",
    "general.resend_control_last_check_time",
    "general.resync_threshold",
    "general.resync_threshold_in_seconds",
    "general.udp_port_base",
    "general.udp_port_range",
    "general.volume_max_db",
    "general.volume_range_db",
    "latencies.default",
    "sessioncontrol.active_state_timeout",
    "sessioncontrol.session_timeout"
  };
  const auto numeric = [](int type) {
    return type == CONFIG_TYPE_INT || type == CONFIG_TYPE_INT64 || type == CONFIG_TYPE_FLOAT;
  };
  for (const char *path : stringSettings) {
    if (const auto *value = config_lookup(&settings, path); value && config_setting_type(value) != CONFIG_TYPE_STRING)
      return std::unexpected(std::string(path) + " must be a string");
  }
  for (const char *path : numericSettings) {
    if (const auto *value = config_lookup(&settings, path); value && !numeric(config_setting_type(value)))
      return std::unexpected(std::string(path) + " must be numeric");
  }
  if (const auto *value = config_lookup(&settings, "general.audio_backend_silent_lead_in_time");
      value && !numeric(config_setting_type(value)) && config_setting_type(value) != CONFIG_TYPE_STRING)
    return std::unexpected("general.audio_backend_silent_lead_in_time must be numeric or auto");
  for (const char *path : {"general.output_format", "pulseaudio.output_format",
                           "general.output_rate", "pulseaudio.output_rate",
                           "general.output_channels", "pulseaudio.output_channels",
                           "general.output_channel_mapping"}) {
    const auto *value = config_lookup(&settings, path);
    if (!value)
      continue;
    const bool strings = std::string_view(path).ends_with("output_format") ||
                         std::string_view(path).ends_with("output_channel_mapping");
    const auto accepted = [strings](const config_setting_t *element) {
      const int type = config_setting_type(element);
      return type == CONFIG_TYPE_STRING || (!strings && type == CONFIG_TYPE_INT);
    };
    const int type = config_setting_type(value);
    if (type == CONFIG_TYPE_ARRAY || type == CONFIG_TYPE_LIST) {
      for (int index = 0; index < config_setting_length(value); ++index) {
        if (!accepted(config_setting_get_elem(value, index)))
          return std::unexpected(std::string(path) + " contains an invalid element type");
      }
    } else if (!accepted(value)) {
      return std::unexpected(std::string(path) + " has an invalid setting type");
    }
  }
  return {};
}
