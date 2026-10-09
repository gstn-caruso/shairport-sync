#include "session/session_state.hpp"
#include "volume/volume_runtime.hpp"
#include <gtest/gtest.h>
#include <cassert>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <unistd.h>
#include <vector>

void *player_thread_func(void *);
static std::mutex observation;
static std::condition_variable changed;
static bool waiting = false, played = false;
static bool waitingAfterPacket = false;
static bool waitingWithArrival = false, referenceAvailable = true;
static bool frameTimeAvailable = true;
static int prerollFrames = 0;
static int requiredPrerollFrames = 0;
static SessionState *activeSession = nullptr;
static uint64_t expectedFrameTime = 999000000;
static int16_t outputSample = 0;
static int outputFrames = 0;
extern "C" int __real_pthread_cond_timedwait(pthread_cond_t *, pthread_mutex_t *, const timespec *);
extern "C" int __wrap_pthread_cond_timedwait(pthread_cond_t *condition, pthread_mutex_t *mutex,
                                             const timespec *deadline) {
  {
    std::lock_guard lock(observation);
    waiting = true;
    if (activeSession && activeSession->statistics.hasPlaybackSinceFlush())
      waitingAfterPacket = true;
    if (activeSession && activeSession->statistics.hasArrivals() &&
        prerollFrames >= requiredPrerollFrames)
      waitingWithArrival = true;
  }
  changed.notify_one();
  return __real_pthread_cond_timedwait(condition, mutex, deadline);
}
extern "C" uint64_t __wrap_get_absolute_time_in_ns() { return 1000000000; }
extern "C" int __wrap_have_timestamp_timing_information(rtsp_conn_info *) { return referenceAvailable; }
extern "C" int __wrap_frame_to_local_time(uint32_t, uint64_t *time, rtsp_conn_info *) {
  *time = expectedFrameTime;
  return frameTimeAvailable ? 0 : -1;
}
extern "C" int __wrap_local_time_to_frame(uint64_t, uint32_t *frame, rtsp_conn_info *) {
  *frame = 0;
  return 0;
}
extern "C" void __wrap_ptp_send_control_message_string(const char *) {}

static int32_t chooseOutput(unsigned, unsigned, unsigned) {
  return CHANNELS_TO_ENCODED_FORMAT(2) | RATE_TO_ENCODED_FORMAT(44100) |
         FORMAT_TO_ENCODED_FORMAT(SPS_FORMAT_S16_LE);
}
static int underrunDelay(long *frames) {
  *frames = -100;
  return 0;
}
static int play(void *buffer, int frames, int type, uint32_t timestamp, uint64_t) {
  if (type == play_samples_are_untimed) {
    std::lock_guard lock(observation);
    assert(frames > 0 && frames <= 4410);
    prerollFrames += frames;
  }
  if (type == play_samples_are_timed && timestamp == 1000) {
    assert(frames >= 0);
    const auto *bytes = static_cast<const uint8_t *>(buffer);
    {
      std::lock_guard lock(observation);
      if (frames > 0)
        outputSample = static_cast<int16_t>(uint16_t(bytes[0]) | uint16_t(bytes[1]) << 8);
      outputFrames = frames;
      played = true;
    }
    changed.notify_one();
  }
  return 0;
}
static void *idleReceiver(void *) {
  for (;;)
    pause();
}
static std::vector<uint8_t> encodedConstant() {
  AVCodecContext *encoder = avcodec_alloc_context3(avcodec_find_encoder(AV_CODEC_ID_ALAC));
  assert(encoder);
  encoder->sample_fmt = AV_SAMPLE_FMT_S16P;
  encoder->sample_rate = 44100;
  av_channel_layout_default(&encoder->ch_layout, 2);
  assert(avcodec_open2(encoder, encoder->codec, nullptr) == 0);
  OwnedAudioFrame frame(av_frame_alloc());
  frame->format = encoder->sample_fmt;
  frame->sample_rate = 44100;
  frame->nb_samples = 352;
  assert(av_channel_layout_copy(&frame->ch_layout, &encoder->ch_layout) == 0);
  assert(av_frame_get_buffer(frame.get(), 0) == 0);
  for (unsigned channel = 0; channel < 2; ++channel)
    for (int sample = 0; sample < frame->nb_samples; ++sample)
      reinterpret_cast<int16_t *>(frame->data[channel])[sample] = 6000;
  assert(avcodec_send_frame(encoder, frame.get()) == 0);
  AVPacket *packet = av_packet_alloc();
  assert(avcodec_receive_packet(encoder, packet) == 0);
  std::vector<uint8_t> bytes(packet->data, packet->data + packet->size);
  av_packet_free(&packet);
  avcodec_free_context(&encoder);
  return bytes;
}
static void checkPlayback(bool hasDelay, uint64_t frameTime, int expectedFrames, bool submit,
                          bool mute = false, bool waitOnly = false, bool anchor = true,
                          int expectedPreroll = 0, bool conversion = true) {
  const auto savedOutput = config.output;
  const auto savedOutputConfiguration = config.current_output_configuration;
  const auto savedDecoder = config.decoder_in_use;
  const auto savedMode = config.playback_mode;
  const auto savedStuffing = config.packet_stuffing;
  const auto savedSync = config.no_sync;
  const auto savedProfile = config.volume_control_profile;
  const auto savedRange = config.volume_range_db;
  const auto savedAutomaticLeadIn = config.audio_backend_silent_lead_in_time_auto;
  const auto savedErrorReported = config.unfixable_error_reported;
  const auto savedSharedLevel = sharedVolumeLevel.current();
  waiting = played = waitingAfterPacket = waitingWithArrival = false;
  referenceAvailable = anchor;
  frameTimeAvailable = conversion;
  prerollFrames = 0;
  requiredPrerollFrames = expectedPreroll;
  expectedFrameTime = frameTime;
  outputFrames = 0;
  outputSample = 0;
  auto packet = encodedConstant();
  SessionState session{};
  activeSession = &session;
  assert(pthread_mutex_init(&session.flush_mutex, nullptr) == 0);
  session.airplay_stream_type = realtime_stream;
  assert(pthread_create(&session.rtp_realtime_audio_thread, nullptr, idleReceiver, nullptr) == 0);
  assert(pthread_create(&session.rtp_ap2_control_thread, nullptr, idleReceiver, nullptr) == 0);
  audio_output backend{};
  backend.get_configuration = chooseOutput;
  backend.play = play;
  backend.delay = hasDelay ? underrunDelay : nullptr;
  config.output = &backend;
  config.decoder_in_use = 1 << decoder_ffmpeg_alac;
  config.playback_mode = ST_stereo;
  config.packet_stuffing = ST_basic;
  config.no_sync = 1;
  config.volume_control_profile = VCP_flat;
  config.volume_range_db = 12;
  config.audio_backend_silent_lead_in_time_auto = 1;
  sharedVolumeLevel.remember(0);
  pthread_t player;
  assert(pthread_create(&player, nullptr, player_thread_func, &session) == 0);
  {
    std::unique_lock lock(observation);
    changed.wait(lock, [] { return waiting; });
  }
  player_volume(-15, &session);
  if (mute) player_volume(-144, &session);
  const int gain = session.volumeControl.pcmSnapshot().gainFixed16;
  assert(gain > 0 && gain < 65536);
  assert(player_put_packet(ALAC_44100_S16_2, 7, 1000, packet.data(), packet.size(), 0, 0,
                           &session) == 352);
  {
    std::unique_lock lock(observation);
    changed.wait(lock, [=] { return waitOnly ? waitingWithArrival : waitingAfterPacket; });
  }
  assert(pthread_cancel(player) == 0);
  void *completion;
  assert(pthread_join(player, &completion) == 0 && completion == PTHREAD_CANCELED);
  activeSession = nullptr;
  assert(pthread_mutex_destroy(&session.flush_mutex) == 0);
  assert(played == submit);
  const auto statistics = session.statistics.snapshot();
  assert(statistics.packets == 1 && statistics.playNumber == (waitOnly ? 0 : 1));
  assert(statistics.frames == (submit ? expectedFrames : 0));
  assert(statistics.measurements == (submit && expectedFrames > 0 ? 1 : 0));
  assert(session.statistics.sessionSummary(1000000000).hasObservedFrame == !waitOnly);
  assert(prerollFrames == expectedPreroll);
  if (submit) {
    assert(outputFrames == expectedFrames);
    if (expectedFrames > 0) {
      const int expected = mute ? 0 : 6000 * gain / 65536;
      assert(outputSample >= expected - 1 && outputSample <= expected + 1);
    }
  }
  config.output = savedOutput;
  config.current_output_configuration = savedOutputConfiguration;
  config.decoder_in_use = savedDecoder;
  config.playback_mode = savedMode;
  config.packet_stuffing = savedStuffing;
  config.no_sync = savedSync;
  config.volume_control_profile = savedProfile;
  config.volume_range_db = savedRange;
  config.audio_backend_silent_lead_in_time_auto = savedAutomaticLeadIn;
  config.unfixable_error_reported = savedErrorReported;
  sharedVolumeLevel.remember(savedSharedLevel);
}

TEST(PlayerVolumeWait, NoDelayUsesUpdatedGainAndFullPacketLength) {
  checkPlayback(false, 999000000, 352, true);
}

TEST(PlayerVolumeWait, UnderrunDelaySkipsFortyFourFramesEvenWhenSyncIsDisabled) {
  checkPlayback(true, 999000000, 308, true);
}

TEST(PlayerVolumeWait, ContinuingDiscardWaitsWithoutSubmittingCallback) {
  checkPlayback(true, 991000000, 0, false);
}

TEST(PlayerVolumeWait, ExactDiscardSubmitsZeroFrames) {
  checkPlayback(true, 992000000, 0, true);
}

TEST(PlayerVolumeWait, MuteSubmitsSilenceAtFullPacketLength) {
  checkPlayback(false, 999000000, 352, true, true);
}

TEST(PlayerVolumeWait, MissingAnchorWaitsWithoutPlayback) {
  checkPlayback(false, 999000000, 0, false, false, true, false);
}

TEST(PlayerVolumeWait, ZeroFrameTimeWaitsWithoutPlayback) {
  checkPlayback(false, 0, 0, false, false, true);
}

TEST(PlayerVolumeWait, NoDelayPrerollSubmitsSilenceThenWaits) {
  checkPlayback(false, 1150000000, 0, false, false, true, true, 6615);
}

TEST(PlayerVolumeWait, DelayPrerollSubmitsSilenceThenWaits) {
  checkPlayback(true, 1050000000, 0, false, false, true, true, 2205);
}

TEST(PlayerVolumeWait, UnavailableFrameTimeConversionWaitsWithoutPlayback) {
  checkPlayback(false, 999000000, 0, false, false, true, true, 0, false);
}
