#include "audio_packet_buffer.hpp"

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
  for (auto &entry : entries_)
    entry.reset();
  synced_ = false;
  read_ = write_ = 0;
  ++revision_;
}
size_t AudioPacketBuffer::occupancy() const {
  Lock lock(mutex_);
  return synced_ ? static_cast<uint16_t>(write_ - read_) : 0;
}
