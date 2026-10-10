#include "audio/buffer/audio_packet_buffer.hpp"
#include "audio/resampling/resampler.hpp"
#include <gtest/gtest.h>
#include <stdexcept>

static OwnedAudioFrame decodedFrame() {
  OwnedAudioFrame frame(av_frame_alloc());
  if (!frame)
    throw std::runtime_error("Cannot allocate decoded packet frame");
  frame->nb_samples = 16;
  frame->format = AV_SAMPLE_FMT_S16P;
  frame->sample_rate = 44100;
  av_channel_layout_default(&frame->ch_layout, 2);
  const auto allocated = av_frame_get_buffer(frame.get(), 0);
  EXPECT_EQ(allocated, 0);
  if (allocated != 0)
    throw std::runtime_error("Cannot allocate decoded packet samples");
  for (int index = 0; index < 16; ++index) {
    reinterpret_cast<int16_t *>(frame->data[0])[index] = index;
    reinterpret_cast<int16_t *>(frame->data[1])[index] = 100 + index;
  }
  return frame;
}
static QueuedAudioPacket packet(uint16_t sequence, uint32_t timestamp) {
  return QueuedAudioPacket::decoded(*AudioFormat::fromSsrc(ALAC_44100_S16_2), sequence,
                                    timestamp, 0, decodedFrame());
}

TEST(AudioPacketBuffer, FactoryFailuresPreserveEmptyAndOverflowAdmissionWindows) {
  AudioPacketBuffer transactional;
  const auto emptyRevision = transactional.revision();
  const auto failedDecode = []() -> QueuedAudioPacket {
    throw std::runtime_error("decode failed");
  };
  EXPECT_THROW(transactional.accept(70, 0, failedDecode), std::runtime_error);
  EXPECT_FALSE(transactional.front());
  EXPECT_EQ(transactional.revision(), emptyRevision);
  auto firstAfterFailure = transactional.accept(71, 1, [] { return packet(71, 100); });
  const auto beforeOverflow = transactional.front();
  ASSERT_TRUE(beforeOverflow);
  EXPECT_THROW(transactional.accept(2000, 2, failedDecode), std::runtime_error);
  EXPECT_EQ(transactional.revision(), beforeOverflow->revision);
  const auto afterOverflow = transactional.front();
  ASSERT_TRUE(afterOverflow);
  EXPECT_EQ(afterOverflow->packet.sequence, 71);
  EXPECT_EQ(transactional.occupancy(), 1);
  auto nextAfterFailure = transactional.accept(72, 3, [] { return packet(72, 116); });
  EXPECT_EQ(nextAfterFailure.kind, ArrivalKind::inOrder);
  EXPECT_EQ(transactional.occupancy(), 2);
  EXPECT_EQ(firstAfterFailure.kind, ArrivalKind::first);
}

TEST(AudioPacketBuffer, ExtractedPacketOwnsDecodedFrameAfterUsedBufferReset) {
  AudioPacketBuffer buffer;
  EXPECT_FALSE(buffer.front());
  EXPECT_EQ(buffer.occupancy(), 0);
  int factories = 0;
  auto accepted = buffer.accept(7, 100, [&] { ++factories; return packet(7, 1000); });
  EXPECT_EQ(accepted.kind, ArrivalKind::first);
  EXPECT_EQ(accepted.samples, 16);
  EXPECT_EQ(factories, 1);
  auto front = buffer.front();
  ASSERT_TRUE(front);
  EXPECT_EQ(front->packet.sequence, 7);
  EXPECT_EQ(front->packet.timestamp, 1000);
  EXPECT_TRUE(front->packet.ready);
  EXPECT_EQ(buffer.occupancy(), 1);
  auto extracted = buffer.takeFrontIf(front->revision);
  ASSERT_TRUE(extracted);
  ASSERT_TRUE(std::holds_alternative<QueuedAudioPacket>(*extracted));
  buffer.reset();
  auto &owned = std::get<QueuedAudioPacket>(*extracted);
  EXPECT_EQ(owned.metadata().frames, 16);
  EXPECT_EQ(owned.decodedSampleFormat(), AV_SAMPLE_FMT_S16P);
  EXPECT_FALSE(buffer.front());
  EXPECT_EQ(buffer.occupancy(), 0);
  const auto afterReset = buffer.accept(65535, 200, [] { return packet(65535, 2000); });
  EXPECT_EQ(afterReset.kind, ArrivalKind::first);
  EXPECT_EQ(buffer.occupancy(), 1);
}

TEST(AudioPacketBuffer, ResetDiscardsQueuedAudioAndStartsANewAdmissionWindow) {
  AudioPacketBuffer buffer;
  buffer.accept(2000, 500, [] { return packet(2000, 5000); });
  const auto beforeReset = buffer.front();
  ASSERT_TRUE(beforeReset);

  buffer.reset();

  EXPECT_FALSE(buffer.front());
  EXPECT_EQ(buffer.occupancy(), 0);
  EXPECT_FALSE(buffer.takeFrontIf(beforeReset->revision));
  const auto accepted = buffer.accept(30, 1000, [] { return packet(30, 8000); });
  EXPECT_EQ(accepted.kind, ArrivalKind::first);
  const auto first = buffer.front();
  ASSERT_TRUE(first);
  EXPECT_EQ(first->packet.sequence, 30);
  EXPECT_EQ(first->packet.timestamp, 8000);
}

TEST(AudioPacketBuffer, ModularLateDuplicateAndOverflowAdmissionPreserveRevisionsAndFactories) {
  AudioPacketBuffer buffer;
  int factories = 0;
  buffer.accept(65535, 200, [&] { ++factories; return packet(65535, 2000); });
  buffer.accept(1, 300, [&] { ++factories; return packet(1, 2032); });
  EXPECT_EQ(buffer.occupancy(), 3);
  auto oldFront = buffer.front();
  ASSERT_TRUE(oldFront);
  auto late = buffer.accept(0, 350, [&] { ++factories; return packet(0, 2016); });
  EXPECT_EQ(late.kind, ArrivalKind::late);
  EXPECT_EQ(factories, 3);
  EXPECT_FALSE(buffer.takeFrontIf(oldFront->revision));
  auto duplicate = buffer.accept(0, 360, [&] { ++factories; return packet(0, 2016); });
  EXPECT_EQ(duplicate.kind, ArrivalKind::duplicate);
  EXPECT_EQ(factories, 3);
  for (uint16_t expected : {uint16_t{65535}, uint16_t{0}, uint16_t{1}}) {
    auto current = buffer.front();
    SCOPED_TRACE(testing::Message() << "Sequence " << expected);
    ASSERT_TRUE(current);
    EXPECT_EQ(current->packet.sequence, expected);
    EXPECT_TRUE(buffer.takeFrontIf(current->revision));
  }
  auto tooLate = buffer.accept(65535, 400, [&] { ++factories; return packet(65535, 2000); });
  EXPECT_EQ(tooLate.kind, ArrivalKind::tooLate);
  EXPECT_EQ(factories, 3);
  auto overflow = buffer.accept(2000, 500, [&] { ++factories; return packet(2000, 5000); });
  EXPECT_EQ(overflow.kind, ArrivalKind::overflow);
  EXPECT_EQ(buffer.occupancy(), 1);
  const auto overflowFront = buffer.front();
  ASSERT_TRUE(overflowFront);
  EXPECT_EQ(overflowFront->packet.sequence, 2000);
}

TEST(AudioPacketBuffer, TrimmedConversionLeavesSharedDecodedFrameUnchanged) {
  Resampler resampler;
  auto source = decodedFrame();
  OwnedAudioFrame shared(av_frame_clone(source.get()));
  ASSERT_TRUE(shared);
  auto trimmed = QueuedAudioPacket::decoded(*AudioFormat::fromSsrc(ALAC_44100_S16_2),
                                            20, 6000, 0, std::move(source));
  ASSERT_TRUE(trimmed.trimBefore(6005));
  EXPECT_EQ(trimmed.metadata().timestamp, 6005);
  EXPECT_EQ(trimmed.metadata().frames, 11);
  ASSERT_TRUE(resampler.configure(trimmed.format(), *trimmed.decodedSampleFormat(), {44100, 2}));
  ASSERT_TRUE(trimmed.convertWith(resampler));
  EXPECT_EQ(trimmed.metadata().frames, 11);
  ASSERT_EQ(trimmed.audioBytes().size(), 44);
  auto samples = reinterpret_cast<const int16_t *>(trimmed.audioBytes().data());
  EXPECT_EQ(samples[0], 5);
  EXPECT_EQ(samples[1], 105);
  EXPECT_EQ(shared->nb_samples, 16);
  EXPECT_EQ(reinterpret_cast<int16_t *>(shared->data[0])[0], 0);
}

TEST(AudioPacketBuffer, MuteAfterTrimmedConversionPreservesPacketDurationAndSilence) {
  Resampler resampler;
  auto preceding = packet(20, 6000);
  ASSERT_TRUE(preceding.trimBefore(6005));
  ASSERT_TRUE(resampler.configure(preceding.format(), *preceding.decodedSampleFormat(), {44100, 2}));
  ASSERT_TRUE(preceding.convertWith(resampler));
  auto muted = QueuedAudioPacket::decoded(*AudioFormat::fromSsrc(ALAC_44100_S16_2),
                                         21, 7000, 0, {});
  muted.mute();
  EXPECT_EQ(muted.samplesDecoded(), 0);
  EXPECT_EQ(muted.metadata().frames, 352);
  ASSERT_TRUE(muted.convertWith(resampler));
  EXPECT_EQ(muted.metadata().frames, 352);
  EXPECT_EQ(muted.audioBytes().size(), 352 * 2 * sizeof(int16_t));
  EXPECT_EQ(std::vector<uint8_t>(muted.audioBytes().begin(), muted.audioBytes().end()),
            std::vector<uint8_t>(muted.audioBytes().size(), 0));
}

TEST(AudioPacketBuffer, FlushHistoryPreservesIdsBoundariesAndStaleRevisionRejection) {
  AudioPacketBuffer buffer;
  buffer.accept(30, 1000, [] { return packet(30, 8000); });
  buffer.accept(31, 1001, [] { return packet(31, 8016); });
  auto beforeFlush = buffer.front();
  ASSERT_TRUE(beforeFlush);
  auto flushId = buffer.requestFlush(8005);
  auto partial = buffer.applyFlush();
  EXPECT_EQ(partial.id, flushId);
  EXPECT_TRUE(partial.flushOutput);
  EXPECT_TRUE(partial.complete);
  EXPECT_FALSE(buffer.takeFrontIf(beforeFlush->revision));
  const auto trimmed = buffer.front();
  ASSERT_TRUE(trimmed);
  EXPECT_EQ(trimmed->packet.timestamp, 8005);
  EXPECT_EQ(trimmed->packet.frames, 11);
  EXPECT_FALSE(buffer.applyFlush().flushOutput);
  auto futureId = buffer.requestFlush(9000);
  auto future = buffer.applyFlush();
  EXPECT_EQ(future.id, futureId);
  EXPECT_TRUE(future.flushOutput);
  EXPECT_FALSE(future.complete);
  EXPECT_TRUE(future.resetTiming);
  EXPECT_EQ(buffer.occupancy(), 0);
  buffer.accept(40, 1100, [] { return packet(40, 9005); });
  auto expired = buffer.applyFlush();
  EXPECT_EQ(expired.id, futureId);
  EXPECT_FALSE(expired.flushOutput);
  EXPECT_TRUE(expired.complete);
  EXPECT_EQ(buffer.occupancy(), 1);
  auto unchanged = buffer.front();
  ASSERT_TRUE(unchanged);
  EXPECT_EQ(buffer.discardPacketsStartingBefore(9005), 0);
  const auto retained = buffer.front();
  ASSERT_TRUE(retained);
  EXPECT_EQ(retained->packet.timestamp, unchanged->packet.timestamp);
  buffer.requestFlush(0);
  auto total = buffer.applyFlush();
  EXPECT_TRUE(total.flushOutput);
  EXPECT_TRUE(total.complete);
  EXPECT_TRUE(total.resetTiming);
  EXPECT_EQ(buffer.occupancy(), 0);
  buffer.accept(50, 1200, [] { return packet(50, 10000); });
  buffer.requestFlush(10016);
  const auto boundary = buffer.applyFlush();
  EXPECT_TRUE(boundary.complete);
  EXPECT_TRUE(boundary.resetTiming);
  EXPECT_EQ(buffer.occupancy(), 0);
  EXPECT_FALSE(buffer.applyFlush().flushOutput);
}

TEST(AudioPacketBuffer, AdmissionObserverReceivesAcceptedAndRejectedClassificationsOnce) {
  AudioPacketBuffer buffer;
  std::vector<ArrivalKind> arrivals;
  arrivals.reserve(7);
  const auto observe = [&](ArrivalKind kind) noexcept { arrivals.push_back(kind); };
  int factories = 0;
  const auto admit = [&](uint16_t sequence) {
    return buffer.accept(sequence, 0, [&] {
      ++factories;
      return packet(sequence, 1000);
    }, {}, observe);
  };
  admit(7);
  admit(9);
  admit(8);
  admit(8);
  admit(6);
  admit(10);
  admit(2000);

  const std::vector<ArrivalKind> expected{ArrivalKind::first, ArrivalKind::ahead,
      ArrivalKind::late, ArrivalKind::duplicate, ArrivalKind::tooLate,
      ArrivalKind::inOrder, ArrivalKind::overflow};
  EXPECT_EQ(arrivals, expected);
  EXPECT_EQ(factories, 5);
  EXPECT_EQ(buffer.occupancy(), 1);
}

TEST(AudioPacketBuffer, FactoryFailureDoesNotNotifyAdmissionObserver) {
  AudioPacketBuffer buffer;
  int notifications = 0;
  const auto observe = [&](ArrivalKind) noexcept { ++notifications; };
  const auto revision = buffer.revision();
  const auto fail = []() -> QueuedAudioPacket { throw std::runtime_error("decode failed"); };
  EXPECT_THROW(buffer.accept(7, 0, fail, {}, observe), std::runtime_error);
  EXPECT_EQ(notifications, 0);
  EXPECT_EQ(buffer.revision(), revision);
  EXPECT_FALSE(buffer.front());

  buffer.accept(7, 0, [] { return packet(7, 1000); }, {}, observe);
  const auto accepted = buffer.front();
  ASSERT_TRUE(accepted);
  EXPECT_THROW(buffer.accept(2000, 0, fail, {}, observe), std::runtime_error);
  EXPECT_EQ(notifications, 1);
  EXPECT_EQ(buffer.revision(), accepted->revision);
  EXPECT_EQ(buffer.occupancy(), 1);
}

TEST(AudioPacketBuffer, EmptyDecodedPacketPreservesPreviouslyRetainedResamplerFrames) {
  Resampler delayed;
  const auto format = *AudioFormat::fromSsrc(ALAC_44100_S16_2);
  ASSERT_TRUE(delayed.configure(format, AV_SAMPLE_FMT_S16P, {48000, 2}));
  auto prior = delayed.convert(*decodedFrame());
  ASSERT_TRUE(prior);
  EXPECT_GT(delayed.retainedFrames(), 0);
  const auto retained = delayed.retainedFrames();
  auto failed = QueuedAudioPacket::decoded(format, 60, 11000, 0, {});
  ASSERT_TRUE(failed.convertWith(delayed));
  EXPECT_TRUE(failed.audioBytes().empty());
  EXPECT_EQ(failed.metadata().frames, 0);
  EXPECT_EQ(delayed.retainedFrames(), retained);
}

TEST(AudioPacketBuffer, WrappedMissingPacketPreservesSnapshotsExtractionAndRevisions) {
  AudioPacketBuffer buffer;
  buffer.accept(65535, 200, [] { return packet(65535, 2000); });
  buffer.accept(1, 300, [] { return packet(1, 2032); });
  EXPECT_EQ(buffer.occupancy(), 3);

  const auto first = buffer.front();
  ASSERT_TRUE(first);
  EXPECT_EQ(first->packet.sequence, 65535);
  EXPECT_EQ(first->packet.timestamp, 2000);
  EXPECT_EQ(first->packet.frames, 16);
  EXPECT_EQ(first->packet.timestampGap, 0);
  EXPECT_EQ(first->packet.encoding, ALAC_44100_S16_2);
  EXPECT_TRUE(first->packet.ready);
  EXPECT_EQ(first->sampleFormat, AV_SAMPLE_FMT_S16P);
  EXPECT_EQ(first->revision, buffer.revision());
  auto extractedFirst = buffer.takeFrontIf(first->revision);
  ASSERT_TRUE(extractedFirst);
  ASSERT_TRUE(std::holds_alternative<QueuedAudioPacket>(*extractedFirst));
  EXPECT_EQ(std::get<QueuedAudioPacket>(*extractedFirst).metadata().sequence, 65535);

  const auto missing = buffer.front();
  ASSERT_TRUE(missing);
  EXPECT_EQ(missing->packet.sequence, 0);
  EXPECT_EQ(missing->packet.timestamp, 0);
  EXPECT_EQ(missing->packet.frames, 0);
  EXPECT_EQ(missing->packet.timestampGap, 0);
  EXPECT_EQ(missing->packet.encoding, SSRC_NONE);
  EXPECT_FALSE(missing->packet.ready);
  EXPECT_EQ(missing->sampleFormat, AV_SAMPLE_FMT_NONE);
  EXPECT_EQ(missing->revision, buffer.revision());
  EXPECT_GT(missing->revision, first->revision);
  EXPECT_FALSE(buffer.takeFrontIf(first->revision));
  EXPECT_EQ(buffer.occupancy(), 2);
  auto extractedMissing = buffer.takeFrontIf(missing->revision);
  ASSERT_TRUE(extractedMissing);
  ASSERT_TRUE(std::holds_alternative<MissingAudioPacket>(*extractedMissing));
  EXPECT_EQ(std::get<MissingAudioPacket>(*extractedMissing).sequence, 0);

  const auto following = buffer.front();
  ASSERT_TRUE(following);
  EXPECT_EQ(following->packet.sequence, 1);
  EXPECT_EQ(following->packet.timestamp, 2032);
  EXPECT_TRUE(following->packet.ready);
  EXPECT_EQ(buffer.occupancy(), 1);
  auto extractedFollowing = buffer.takeFrontIf(following->revision);
  ASSERT_TRUE(extractedFollowing);
  ASSERT_TRUE(std::holds_alternative<QueuedAudioPacket>(*extractedFollowing));
  EXPECT_EQ(std::get<QueuedAudioPacket>(*extractedFollowing).metadata().sequence, 1);
  EXPECT_EQ(buffer.occupancy(), 0);
  EXPECT_FALSE(buffer.front());
}
