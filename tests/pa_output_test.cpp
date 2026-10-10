#include "audio/output/audio.h"
#include "runtime/common.h"
#include <pulse/pulseaudio.h>
#include <gtest/gtest.h>
#include <algorithm>
#include <vector>

extern audio_output audio_pa;
extern pa_stream *stream;
extern pa_context *context;
extern pa_threaded_mainloop *mainloop;

namespace {
bool corked;
unsigned starts;
unsigned canceledReservations;
size_t writableBytes;
enum class WriteFailure { none, zeroReservation, nullReservation, subframeReservation, rejectedWrite };
WriteFailure writeFailure;
pa_stream_request_cb_t writeCallback;
std::vector<unsigned char> writable(176400);
std::vector<unsigned char> delivered;
}

extern "C" {
void __wrap_pa_threaded_mainloop_lock(pa_threaded_mainloop *) {}
void __wrap_pa_threaded_mainloop_unlock(pa_threaded_mainloop *) {}
void __wrap_pa_threaded_mainloop_free(pa_threaded_mainloop *) {}
pa_stream *__wrap_pa_stream_new(pa_context *, const char *, const pa_sample_spec *, const pa_channel_map *) {
  return reinterpret_cast<pa_stream *>(1);
}
void __wrap_pa_stream_set_state_callback(pa_stream *, pa_stream_notify_cb_t, void *) {}
void __wrap_pa_stream_set_write_callback(pa_stream *, pa_stream_request_cb_t callback, void *) {
  writeCallback = callback;
}
int __wrap_pa_stream_connect_playback(pa_stream *, const char *, const pa_buffer_attr *, pa_stream_flags_t, const pa_cvolume *, pa_stream *) { corked = true; return 0; }
pa_stream_state_t __wrap_pa_stream_get_state(const pa_stream *) { return PA_STREAM_READY; }
int __wrap_pa_stream_is_corked(const pa_stream *) { return corked; }
int __wrap_pa_stream_is_suspended(const pa_stream *) { return 0; }
pa_operation *__wrap_pa_stream_cork(pa_stream *, int value, pa_stream_success_cb_t, void *) {
  corked = value;
  if (!value) ++starts;
  return nullptr;
}
pa_operation *__wrap_pa_stream_flush(pa_stream *, pa_stream_success_cb_t, void *) { delivered.clear(); return nullptr; }
int __wrap_pa_stream_disconnect(pa_stream *) { return 0; }
void __wrap_pa_stream_unref(pa_stream *) {}
size_t __wrap_pa_stream_writable_size(const pa_stream *) { return writableBytes; }
int __wrap_pa_stream_begin_write(pa_stream *, void **data, size_t *size) {
  *size = std::min(*size, writable.size());
  *data = writable.data();
  if (writeFailure == WriteFailure::zeroReservation) *size = 0;
  if (writeFailure == WriteFailure::nullReservation) *data = nullptr;
  if (writeFailure == WriteFailure::subframeReservation) *size = 3;
  return 0;
}
int __wrap_pa_stream_cancel_write(pa_stream *) { ++canceledReservations; return 0; }
int __wrap_pa_stream_write(pa_stream *, const void *data, size_t size, pa_free_cb_t, int64_t, pa_seek_mode_t) {
  if (writeFailure == WriteFailure::rejectedWrite) return -1;
  auto bytes = static_cast<const unsigned char *>(data);
  delivered.insert(delivered.end(), bytes, bytes + size);
  return 0;
}
}

class PaOutput : public testing::Test {
protected:
  void SetUp() override {
    delivered.clear(); starts = 0; canceledReservations = 0; writableBytes = writable.size(); writeFailure = WriteFailure::none;
    context = nullptr;
    mainloop = reinterpret_cast<pa_threaded_mainloop *>(1);
    ASSERT_EQ(audio_pa.configure(CHANNELS_TO_ENCODED_FORMAT(2) | RATE_TO_ENCODED_FORMAT(44100) | FORMAT_TO_ENCODED_FORMAT(SPS_FORMAT_S16_LE), nullptr), 0);
  }
  void TearDown() override { audio_pa.deinit(); }
  void play(std::vector<unsigned char> &bytes) {
    ASSERT_EQ(audio_pa.play(bytes.data(), bytes.size() / 4, 0, 0, 0), 0);
  }
};

TEST_F(PaOutput, QuarterSecondStartsAndDeliversPcm) {
  std::vector<unsigned char> bytes(44100, 42);
  play(bytes);
  EXPECT_EQ(starts, 1u);
  EXPECT_EQ(delivered, bytes);
  writeCallback(stream, bytes.size(), nullptr);
  EXPECT_EQ(delivered, bytes);
}

TEST_F(PaOutput, OneFrameStartsWithoutWaitingForMorePcm) {
  std::vector<unsigned char> bytes{1, 2, 3, 4};
  play(bytes);
  EXPECT_EQ(starts, 1u);
  EXPECT_EQ(delivered, bytes);
  writeCallback(stream, bytes.size(), nullptr);
  EXPECT_EQ(delivered, bytes);
}

TEST_F(PaOutput, EmptyAndRejectedWritesDoNotStartPlayback) {
  std::vector<unsigned char> empty;
  play(empty);
  EXPECT_EQ(starts, 0u);
  writableBytes = 0;
  std::vector<unsigned char> full(176400, 9);
  play(full);
  EXPECT_EQ(starts, 1u);
  corked = true;
  std::vector<unsigned char> frame(4, 1);
  play(frame);
  EXPECT_EQ(starts, 1u);
  writeCallback(stream, full.size(), nullptr);
  EXPECT_EQ(delivered, full);
}

TEST_F(PaOutput, SeveralSmallWritesDeliverFifoWithoutExtraCallbacks) {
  std::vector<unsigned char> first{1, 2, 3, 4};
  std::vector<unsigned char> second{5, 6, 7, 8};
  play(first); play(second);
  EXPECT_EQ(delivered, (std::vector<unsigned char>{1, 2, 3, 4, 5, 6, 7, 8}));
  EXPECT_EQ(starts, 1u);
}

TEST_F(PaOutput, FlushDiscardsQueuedPcmAndOneFrameResumes) {
  writableBytes = 0;
  std::vector<unsigned char> old(8, 7);
  play(old);
  audio_pa.flush();
  EXPECT_TRUE(corked);
  writableBytes = writable.size();
  std::vector<unsigned char> next{1, 2, 3, 4};
  play(next);
  EXPECT_EQ(delivered, next);
  EXPECT_EQ(starts, 2u);
}

TEST_F(PaOutput, EnqueueAfterEmptyCallbackMakesProgress) {
  writeCallback(stream, 4096, nullptr);
  EXPECT_TRUE(delivered.empty());
  std::vector<unsigned char> frame{1, 2, 3, 4};
  play(frame);
  EXPECT_EQ(delivered, frame);
  EXPECT_EQ(starts, 1u);
}

TEST_F(PaOutput, ZeroReservationReturnsAndPreservesPcmForRetry) {
  writeFailure = WriteFailure::zeroReservation;
  std::vector<unsigned char> frame{1, 2, 3, 4};
  play(frame);
  EXPECT_TRUE(delivered.empty());
  EXPECT_EQ(canceledReservations, 1u);
  writeFailure = WriteFailure::none;
  writeCallback(stream, 4, nullptr);
  EXPECT_EQ(delivered, frame);
}

TEST_F(PaOutput, NullReservationReturnsAndPreservesPcmForRetry) {
  writeFailure = WriteFailure::nullReservation;
  std::vector<unsigned char> frame{1, 2, 3, 4};
  play(frame);
  EXPECT_TRUE(delivered.empty());
  EXPECT_EQ(canceledReservations, 1u);
  writeFailure = WriteFailure::none;
  writeCallback(stream, 4, nullptr);
  EXPECT_EQ(delivered, frame);
}

TEST_F(PaOutput, RejectedServerWritePreservesPcmForRetry) {
  writeFailure = WriteFailure::rejectedWrite;
  std::vector<unsigned char> frame{1, 2, 3, 4};
  play(frame);
  EXPECT_TRUE(delivered.empty());
  EXPECT_EQ(canceledReservations, 1u);
  writeFailure = WriteFailure::none;
  writeCallback(stream, 4, nullptr);
  EXPECT_EQ(delivered, frame);
}

TEST_F(PaOutput, SubframeReservationIsCanceledAndPreservesPcmForRetry) {
  writeFailure = WriteFailure::subframeReservation;
  std::vector<unsigned char> frame{1, 2, 3, 4};
  play(frame);
  EXPECT_TRUE(delivered.empty());
  EXPECT_EQ(canceledReservations, 1u);
  writeFailure = WriteFailure::none;
  writeCallback(stream, 4, nullptr);
  EXPECT_EQ(delivered, frame);
  EXPECT_EQ(canceledReservations, 1u);
}
