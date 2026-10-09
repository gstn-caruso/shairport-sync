#include "audio_packet_buffer.hpp"
#include <bit>

ArrivalKind AudioPacketBuffer::prepareAdmission(uint16_t sequence, uint64_t now) {
  if (!synced_) {
    read_ = write_ = sequence;
    synced_ = true;
    return ArrivalKind::first;
  }
  const auto ahead = std::bit_cast<int16_t>(static_cast<uint16_t>(sequence - write_));
  if (ahead < 0) {
    const uint16_t position = sequence - read_;
    if (position >= static_cast<uint16_t>(write_ - read_))
      return ArrivalKind::tooLate;
    const auto &entry = entries_[sequence % capacity];
    if (!entry || entry->sequence != sequence)
      return ArrivalKind::tooLate;
    return entry->packet ? ArrivalKind::duplicate : ArrivalKind::late;
  }
  if (size_t(static_cast<uint16_t>(write_ - read_)) + ahead + 1 > capacity) {
    resetUnderLock();
    read_ = write_ = sequence;
    synced_ = true;
    return ArrivalKind::overflow;
  }
  for (uint16_t missing = write_; missing != sequence; ++missing)
    entries_[missing % capacity] = Entry{missing, now, std::nullopt};
  return ahead == 0 ? ArrivalKind::inOrder : ArrivalKind::ahead;
}

std::optional<AudioPacketBuffer::Front> AudioPacketBuffer::front() const {
  Lock lock(mutex_);
  if (!synced_ || read_ == write_)
    return std::nullopt;
  const auto &entry = entries_[read_ % capacity];
  return Front{entry->packet ? entry->packet->metadata() :
      AudioPacketMetadata{read_, 0, 0, 0, SSRC_NONE, false}, revision_};
}
std::optional<BufferedAudioPacket> AudioPacketBuffer::takeFrontIf(uint64_t revision) {
  Lock lock(mutex_);
  if (revision != revision_ || !synced_ || read_ == write_)
    return std::nullopt;
  auto &entry = entries_[read_ % capacity];
  BufferedAudioPacket packet = entry->packet ? BufferedAudioPacket(std::move(*entry->packet)) :
                                              BufferedAudioPacket(MissingAudioPacket{read_});
  entry.reset();
  ++read_;
  ++revision_;
  return packet;
}
void AudioPacketBuffer::reset() {
  Lock lock(mutex_);
  resetUnderLock();
  ++revision_;
}
void AudioPacketBuffer::resetUnderLock() {
  for (auto &entry : entries_)
    entry.reset();
  synced_ = false;
  read_ = write_ = 0;
}
size_t AudioPacketBuffer::occupancy() const {
  Lock lock(mutex_);
  return synced_ ? static_cast<uint16_t>(write_ - read_) : 0;
}
