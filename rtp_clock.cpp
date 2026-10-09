#include "rtp_clock.hpp"
#include <bit>

clock_status_t RtpClock::status() const noexcept { return status_; }
clock_status_t RtpClock::observe(const ClockSample &sample, uint64_t now) noexcept {
  if (!remote_)
    return status_ = clock_no_anchor_info;
  status_ = sample.status;
  if (status_ != clock_ok)
    return status_;
  if (std::bit_cast<int64_t>(now - sample.mastershipStart) < 400000000)
    return status_ = clock_not_valid;
  if (sample.id != remote_->id) {
    if (!local_)
      return status_ = clock_not_valid;
    if (std::bit_cast<int64_t>(now - local_->updatedAt) > 5000000000) {
      remote_->time = local_->time + sample.offset;
      remote_->id = sample.id;
    }
    return status_;
  }
  auto validSince = local_ ? local_->validSince : sample.mastershipStart;
  local_ = LocalAnchor{remote_->frame, remote_->time - sample.offset, now, validSince};
  return status_;
}

void RtpClock::setAnchor(uint64_t id, uint32_t frame, uint64_t time, uint64_t now) noexcept {
  if (remote_ && local_ && remote_->id == id &&
      (remote_->frame != frame || remote_->time != time) &&
      std::bit_cast<int64_t>(now - local_->validSince) < 5000000000)
    local_.reset();
  remote_ = RemoteAnchor{id, frame, time};
}

void RtpClock::reset() noexcept {
  remote_.reset();
  local_.reset();
  status_ = clock_no_anchor_info;
}

std::optional<uint32_t> RtpClock::anchorFrame(uint32_t rate, double latency) const noexcept {
  if (!local_)
    return std::nullopt;
  auto adjustment = static_cast<int32_t>(latency * rate);
  return local_->frame - static_cast<uint32_t>(adjustment);
}

std::optional<uint64_t> RtpClock::localTimeForFrame(uint32_t frame, uint32_t rate, double latency) const noexcept {
  if (status_ != clock_ok || !local_ || rate == 0)
    return std::nullopt;
  auto difference = std::bit_cast<int32_t>(frame - *anchorFrame(rate, latency));
  auto elapsed = int64_t{difference} * 1000000000 / rate;
  return local_->time + static_cast<uint64_t>(elapsed);
}

std::optional<uint32_t> RtpClock::frameForLocalTime(uint64_t time, uint32_t rate, double latency) const noexcept {
  if (status_ != clock_ok || !local_ || rate == 0)
    return std::nullopt;
  auto elapsed = std::bit_cast<int64_t>(time - local_->time);
  auto difference = elapsed * rate / 1000000000;
  return *anchorFrame(rate, latency) + static_cast<uint32_t>(difference);
}
