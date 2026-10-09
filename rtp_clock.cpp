#include "rtp_clock.hpp"

clock_status_t RtpClock::status() const noexcept { return status_; }
clock_status_t RtpClock::observe(const ClockSample &, uint64_t) noexcept { return status_; }
std::optional<uint32_t> RtpClock::anchorFrame(uint32_t, double) const noexcept { return std::nullopt; }
std::optional<uint64_t> RtpClock::localTimeForFrame(uint32_t, uint32_t, double) const noexcept {
  return std::nullopt;
}
std::optional<uint32_t> RtpClock::frameForLocalTime(uint64_t, uint32_t, double) const noexcept {
  return std::nullopt;
}
