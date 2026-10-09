#include "audio_packet_buffer.hpp"
#include "resampler.hpp"
#include <cassert>

static QueuedAudioPacket packet(uint16_t sequence, uint32_t timestamp) {
  OwnedAudioFrame frame(av_frame_alloc());
  frame->nb_samples = 16;
  frame->format = AV_SAMPLE_FMT_S16P;
  frame->sample_rate = 44100;
  av_channel_layout_default(&frame->ch_layout, 2);
  assert(av_frame_get_buffer(frame.get(), 0) == 0);
  for (int index = 0; index < 16; ++index) {
    reinterpret_cast<int16_t *>(frame->data[0])[index] = index;
    reinterpret_cast<int16_t *>(frame->data[1])[index] = 100 + index;
  }
  return QueuedAudioPacket::decoded(*AudioFormat::fromSsrc(ALAC_44100_S16_2), sequence,
                                    timestamp, 0, std::move(frame));
}
int main() {
  AudioPacketBuffer buffer;
  assert(!buffer.front() && buffer.occupancy() == 0);
  int factories = 0;
  auto accepted = buffer.accept(7, 100, [&] { ++factories; return packet(7, 1000); });
  assert(accepted.kind == ArrivalKind::first && accepted.samples == 16 && factories == 1);
  auto front = buffer.front();
  assert(front && front->packet.sequence == 7 && front->packet.timestamp == 1000);
  assert(front->packet.ready && buffer.occupancy() == 1);
  auto extracted = buffer.takeFrontIf(front->revision);
  assert(extracted && std::holds_alternative<QueuedAudioPacket>(*extracted));
  buffer.reset();
  auto &owned = std::get<QueuedAudioPacket>(*extracted);
  assert(owned.metadata().frames == 16 && owned.decodedSampleFormat() == AV_SAMPLE_FMT_S16P);
  assert(!buffer.front() && buffer.occupancy() == 0);
  factories = 0;
  buffer.accept(65535, 200, [&] { ++factories; return packet(65535, 2000); });
  buffer.accept(1, 300, [&] { ++factories; return packet(1, 2032); });
  assert(buffer.occupancy() == 3);
  auto oldFront = buffer.front();
  auto late = buffer.accept(0, 350, [&] { ++factories; return packet(0, 2016); });
  assert(late.kind == ArrivalKind::late && factories == 3);
  assert(!buffer.takeFrontIf(oldFront->revision));
  auto duplicate = buffer.accept(0, 360, [&] { ++factories; return packet(0, 2016); });
  assert(duplicate.kind == ArrivalKind::duplicate && factories == 3);
  for (uint16_t expected : {uint16_t{65535}, uint16_t{0}, uint16_t{1}}) {
    auto current = buffer.front();
    assert(current && current->packet.sequence == expected);
    assert(buffer.takeFrontIf(current->revision));
  }
  auto tooLate = buffer.accept(65535, 400, [&] { ++factories; return packet(65535, 2000); });
  assert(tooLate.kind == ArrivalKind::tooLate && factories == 3);
  auto overflow = buffer.accept(2000, 500, [&] { ++factories; return packet(2000, 5000); });
  assert(overflow.kind == ArrivalKind::overflow && buffer.occupancy() == 1);
  assert(buffer.front()->packet.sequence == 2000);
  auto trimmed = packet(20, 6000);
  assert(trimmed.trimBefore(6005));
  assert(trimmed.metadata().timestamp == 6005 && trimmed.metadata().frames == 11);
  Resampler resampler;
  assert(resampler.configure(trimmed.format(), *trimmed.decodedSampleFormat(), {44100, 2}));
  assert(trimmed.convertWith(resampler));
  assert(trimmed.metadata().frames == 11 && trimmed.audioBytes().size() == 44);
  auto samples = reinterpret_cast<const int16_t *>(trimmed.audioBytes().data());
  assert(samples[0] == 5 && samples[1] == 105);
  auto muted = QueuedAudioPacket::decoded(*AudioFormat::fromSsrc(ALAC_44100_S16_2),
                                         21, 7000, 0, {});
  muted.mute();
  assert(muted.samplesDecoded() == 0 && muted.metadata().frames == 352);
  assert(muted.convertWith(resampler));
  assert(muted.metadata().frames == 352);
  for (auto byte : muted.audioBytes())
    assert(byte == 0);
  buffer.reset();
  buffer.accept(30, 1000, [] { return packet(30, 8000); });
  buffer.accept(31, 1001, [] { return packet(31, 8016); });
  auto beforeFlush = buffer.front();
  auto flushId = buffer.requestFlush(8005);
  auto partial = buffer.applyFlush();
  assert(partial.id == flushId && partial.flushOutput && partial.complete);
  assert(!buffer.takeFrontIf(beforeFlush->revision));
  assert(buffer.front()->packet.timestamp == 8005 && buffer.front()->packet.frames == 11);
  assert(!buffer.applyFlush().flushOutput);
  auto futureId = buffer.requestFlush(9000);
  auto future = buffer.applyFlush();
  assert(future.id == futureId && future.flushOutput && !future.complete && future.resetTiming);
  assert(buffer.occupancy() == 0);
  buffer.accept(40, 1100, [] { return packet(40, 9005); });
  auto expired = buffer.applyFlush();
  assert(expired.id == futureId && !expired.flushOutput && expired.complete);
  assert(buffer.occupancy() == 1);
  auto unchanged = buffer.front();
  assert(buffer.dropOutdatedBefore(9010) == 0);
  assert(buffer.front()->packet.timestamp == unchanged->packet.timestamp);
  buffer.requestFlush(0);
  auto total = buffer.applyFlush();
  assert(total.flushOutput && total.complete && total.resetTiming && buffer.occupancy() == 0);
}
