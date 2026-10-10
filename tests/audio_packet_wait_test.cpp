#include "audio/buffer/audio_packet_buffer.hpp"
#include <gtest/gtest.h>
#include <atomic>
#include <stdexcept>
#include <condition_variable>
#include <mutex>

static std::mutex observation;
static std::condition_variable entered;
static bool waiting;
static std::atomic<unsigned> framesReleased;
extern "C" int __real_pthread_cond_timedwait(pthread_cond_t *, pthread_mutex_t *, const timespec *);
extern "C" void __real_av_frame_free(AVFrame **);
extern "C" void __wrap_av_frame_free(AVFrame **frame) {
  if (frame && *frame)
    ++framesReleased;
  __real_av_frame_free(frame);
}
extern "C" int __wrap_pthread_cond_timedwait(pthread_cond_t *condition, pthread_mutex_t *mutex,
                                             const timespec *deadline) {
  {
    std::lock_guard lock(observation);
    waiting = true;
  }
  entered.notify_one();
  return __real_pthread_cond_timedwait(condition, mutex, deadline);
}
static QueuedAudioPacket packet(uint16_t sequence) {
  OwnedAudioFrame frame(av_frame_alloc());
  if (!frame)
    throw std::runtime_error("Cannot allocate waiting packet frame");
  frame->nb_samples = 16;
  frame->format = AV_SAMPLE_FMT_S16P;
  frame->sample_rate = 44100;
  av_channel_layout_default(&frame->ch_layout, 2);
  const auto allocated = av_frame_get_buffer(frame.get(), 0);
  EXPECT_EQ(allocated, 0);
  if (allocated != 0)
    throw std::runtime_error("Cannot allocate waiting packet samples");
  return QueuedAudioPacket::decoded(*AudioFormat::fromSsrc(ALAC_44100_S16_2), sequence,
                                    1000, 0, std::move(frame));
}
static void *waitWithExtractedPacket(void *argument) {
  auto &buffer = *static_cast<AudioPacketBuffer *>(argument);
  auto front = buffer.front();
  auto held = buffer.takeFrontIf(front->revision);
  timespec deadline;
  clock_gettime(CLOCK_REALTIME, &deadline);
  deadline.tv_sec += 3;
  buffer.waitForChange(buffer.revision(), deadline);
  return nullptr;
}
TEST(AudioPacketWait, ResetSignalArrivingBeforeWaitIsNotLost) {
  AudioPacketBuffer buffer;
  auto earlier = buffer.revision();
  buffer.reset();
  EXPECT_EQ(buffer.waitForChange(earlier, {0, 0}), 0);
}

TEST(AudioPacketWait, DeferredCancellationReleasesExtractedFrameAndUnlocksBuffer) {
  AudioPacketBuffer buffer;
  {
    std::lock_guard lock(observation);
    waiting = false;
  }
  buffer.accept(99, 0, [] { return packet(99); });
  const auto released = framesReleased.load();
  pthread_t thread;
  ASSERT_EQ(pthread_create(&thread, nullptr, waitWithExtractedPacket, &buffer), 0);
  {
    std::unique_lock lock(observation);
    entered.wait(lock, [] { return waiting; });
  }
  EXPECT_EQ(pthread_cancel(thread), 0);
  void *completion = nullptr;
  EXPECT_EQ(pthread_join(thread, &completion), 0);
  EXPECT_EQ(completion, PTHREAD_CANCELED);
  EXPECT_EQ(framesReleased.load(), released + 1);
  buffer.accept(100, 1, [] { return packet(100); });
  EXPECT_EQ(buffer.occupancy(), 1);
}
