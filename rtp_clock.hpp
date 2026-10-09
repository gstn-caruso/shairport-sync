#pragma once

#include "clock_status.h"
#include <cstdint>
#include <optional>

struct ClockSample {
  clock_status_t status;
  uint64_t id;
  uint64_t sampleTime;
  uint64_t offset;
  uint64_t mastershipStart;
};

class RtpClock {
public:
  clock_status_t status() const noexcept;
  clock_status_t observe(const ClockSample &sample, uint64_t now) noexcept;
  void setAnchor(uint64_t id, uint32_t frame, uint64_t time, uint64_t now) noexcept;
  void reset() noexcept;
  std::optional<uint32_t> anchorFrame(uint32_t rate, double latency) const noexcept;
  std::optional<uint64_t> localTimeForFrame(uint32_t frame, uint32_t rate, double latency) const noexcept;
  std::optional<uint32_t> frameForLocalTime(uint64_t time, uint32_t rate, double latency) const noexcept;

private:
  struct RemoteAnchor {
    uint64_t id;
    uint32_t frame;
    uint64_t time;
  };
  struct LocalAnchor {
    uint32_t frame;
    uint64_t time;
    uint64_t updatedAt;
    uint64_t validSince;
  };
  std::optional<RemoteAnchor> remote_;
  std::optional<LocalAnchor> local_;
  clock_status_t status_ = clock_no_anchor_info;
};
