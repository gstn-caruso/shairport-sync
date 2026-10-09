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
  advanceRevision();
  return packet;
}
void AudioPacketBuffer::reset() {
  Lock lock(mutex_);
  resetUnderLock();
  advanceRevision();
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
uint64_t AudioPacketBuffer::revision() const {
  Lock lock(mutex_);
  return revision_;
}
void AudioPacketBuffer::advanceRevision() {
  ++revision_;
  pthread_cond_broadcast(&changed_);
}
void AudioPacketBuffer::unlockWaitingMutex(void *mutex) {
  pthread_mutex_unlock(static_cast<pthread_mutex_t *>(mutex));
}
int AudioPacketBuffer::waitForChange(uint64_t revision, timespec deadline) {
  int result = 0;
  pthread_mutex_lock(&mutex_);
  pthread_cleanup_push(unlockWaitingMutex, &mutex_);
  while (revision == revision_ && result == 0)
    result = pthread_cond_timedwait(&changed_, &mutex_, &deadline);
  if (revision != revision_)
    result = 0;
  pthread_cleanup_pop(1);
  return result;
}
