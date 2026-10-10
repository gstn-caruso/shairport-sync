#include "session/session_state.hpp"
#include "volume/volume_runtime.hpp"
#include <gtest/gtest.h>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <functional>
#include <mutex>
#include <thread>
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
class ArrivalPublicationProbe {
public:
  void pauseProducer() {
    std::unique_lock lock(observation);
    paused_ = true;
    changed.notify_all();
    changed.wait(lock, [&] { return released_; });
  }
  void verifyNoOutputBeforePublication() {
    std::unique_lock lock(observation);
    const bool paused = changed.wait_for(lock, std::chrono::seconds(1), [&] { return paused_; });
    EXPECT_TRUE(paused);
    if (paused)
      EXPECT_FALSE(changed.wait_for(lock, std::chrono::milliseconds(250), [] {
        return prerollFrames > 0;
      }));
    released_ = true;
    changed.notify_all();
  }
private:
  bool paused_ = false, released_ = false;
};
static ArrivalPublicationProbe *arrivalPublicationProbe = nullptr;
extern "C" void __real__ZN14PlaybackTiming9onArrivalE11ArrivalKind(PlaybackTiming *, ArrivalKind);
extern "C" void __wrap__ZN14PlaybackTiming9onArrivalE11ArrivalKind(PlaybackTiming *timing,
                                                                  ArrivalKind kind) {
  if (arrivalPublicationProbe && kind == ArrivalKind::first)
    arrivalPublicationProbe->pauseProducer();
  __real__ZN14PlaybackTiming9onArrivalE11ArrivalKind(timing, kind);
}
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
    EXPECT_GT(frames, 0);
    EXPECT_LE(frames, 4410);
    prerollFrames += frames;
    changed.notify_all();
  }
  if (type == play_samples_are_timed && timestamp == 1000) {
    EXPECT_GE(frames, 0);
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
  const auto freeEncoder = [](AVCodecContext *context) { avcodec_free_context(&context); };
  std::unique_ptr<AVCodecContext, decltype(freeEncoder)> encoder(
      avcodec_alloc_context3(avcodec_find_encoder(AV_CODEC_ID_ALAC)), freeEncoder);
  EXPECT_TRUE(encoder);
  if (!encoder)
    return {};
  encoder->sample_fmt = AV_SAMPLE_FMT_S16P;
  encoder->sample_rate = 44100;
  av_channel_layout_default(&encoder->ch_layout, 2);
  const auto opened = avcodec_open2(encoder.get(), encoder->codec, nullptr);
  EXPECT_EQ(opened, 0);
  if (opened != 0)
    return {};
  OwnedAudioFrame frame(av_frame_alloc());
  EXPECT_TRUE(frame);
  if (!frame)
    return {};
  frame->format = encoder->sample_fmt;
  frame->sample_rate = 44100;
  frame->nb_samples = 352;
  const auto layoutCopied = av_channel_layout_copy(&frame->ch_layout, &encoder->ch_layout);
  EXPECT_EQ(layoutCopied, 0);
  if (layoutCopied != 0)
    return {};
  const auto allocated = av_frame_get_buffer(frame.get(), 0);
  EXPECT_EQ(allocated, 0);
  if (allocated != 0)
    return {};
  for (unsigned channel = 0; channel < 2; ++channel)
    for (int sample = 0; sample < frame->nb_samples; ++sample)
      reinterpret_cast<int16_t *>(frame->data[channel])[sample] = 6000;
  const auto sent = avcodec_send_frame(encoder.get(), frame.get());
  EXPECT_EQ(sent, 0);
  if (sent != 0)
    return {};
  const auto freePacket = [](AVPacket *packet) { av_packet_free(&packet); };
  std::unique_ptr<AVPacket, decltype(freePacket)> packet(av_packet_alloc(), freePacket);
  EXPECT_TRUE(packet);
  if (!packet)
    return {};
  const auto received = avcodec_receive_packet(encoder.get(), packet.get());
  EXPECT_EQ(received, 0);
  if (received != 0)
    return {};
  std::vector<uint8_t> bytes(packet->data, packet->data + packet->size);
  return bytes;
}
struct PlaybackScenario {
  bool hasDelay = false;
  uint64_t frameTime = 999000000;
  int expectedFrames = 352;
  bool submit = true, mute = false, waitOnly = false, anchor = true;
  int expectedPreroll = 0;
  bool conversion = true;
};
static void checkPlayback(PlaybackScenario scenario) {
  const auto [hasDelay, frameTime, expectedFrames, submit, mute, waitOnly, anchor,
              expectedPreroll, conversion] = scenario;
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
  struct RestoreConfiguration {
    std::function<void()> restore;
    ~RestoreConfiguration() { restore(); }
  } restore{[&] {
    activeSession = nullptr;
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
  }};
  waiting = played = waitingAfterPacket = waitingWithArrival = false;
  referenceAvailable = anchor;
  frameTimeAvailable = conversion;
  prerollFrames = 0;
  requiredPrerollFrames = expectedPreroll;
  expectedFrameTime = frameTime;
  outputFrames = 0;
  outputSample = 0;
  auto packet = encodedConstant();
  ASSERT_FALSE(packet.empty());
  SessionState session{};
  activeSession = &session;
  ASSERT_EQ(pthread_mutex_init(&session.flush_mutex, nullptr), 0);
  session.airplay_stream_type = realtime_stream;
  const auto audioStarted = pthread_create(&session.rtp_realtime_audio_thread, nullptr, idleReceiver, nullptr);
  if (audioStarted != 0)
    pthread_mutex_destroy(&session.flush_mutex);
  ASSERT_EQ(audioStarted, 0);
  const auto controlStarted = pthread_create(&session.rtp_ap2_control_thread, nullptr, idleReceiver, nullptr);
  if (controlStarted != 0) {
    pthread_cancel(session.rtp_realtime_audio_thread);
    pthread_join(session.rtp_realtime_audio_thread, nullptr);
    pthread_mutex_destroy(&session.flush_mutex);
  }
  ASSERT_EQ(controlStarted, 0);
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
  const auto playerStarted = pthread_create(&player, nullptr, player_thread_func, &session);
  if (playerStarted != 0) {
    pthread_cancel(session.rtp_realtime_audio_thread);
    pthread_cancel(session.rtp_ap2_control_thread);
    pthread_join(session.rtp_realtime_audio_thread, nullptr);
    pthread_join(session.rtp_ap2_control_thread, nullptr);
    pthread_mutex_destroy(&session.flush_mutex);
  }
  ASSERT_EQ(playerStarted, 0);
  {
    std::unique_lock lock(observation);
    if (arrivalPublicationProbe)
      EXPECT_TRUE(changed.wait_for(lock, std::chrono::seconds(1), [] { return waiting; }));
    else
      changed.wait(lock, [] { return waiting; });
  }
  player_volume(-15, &session);
  if (mute) player_volume(-144, &session);
  const int gain = session.volumeControl.pcmSnapshot().gainFixed16;
  EXPECT_GT(gain, 0);
  EXPECT_LT(gain, 65536);
  const auto publishPacket = [&] {
    EXPECT_EQ(player_put_packet(ALAC_44100_S16_2, 7, 1000, packet.data(), packet.size(), 0, 0,
                                &session), 352);
  };
  if (arrivalPublicationProbe) {
    std::thread producer(publishPacket);
    arrivalPublicationProbe->verifyNoOutputBeforePublication();
    producer.join();
  } else {
    publishPacket();
  }
  {
    std::unique_lock lock(observation);
    if (arrivalPublicationProbe)
      EXPECT_TRUE(changed.wait_for(lock, std::chrono::seconds(1), [=] {
        return waitOnly ? waitingWithArrival : waitingAfterPacket;
      }));
    else
      changed.wait(lock, [=] { return waitOnly ? waitingWithArrival : waitingAfterPacket; });
  }
  EXPECT_EQ(pthread_cancel(player), 0);
  void *completion = nullptr;
  EXPECT_EQ(pthread_join(player, &completion), 0);
  EXPECT_EQ(completion, PTHREAD_CANCELED);
  activeSession = nullptr;
  EXPECT_EQ(pthread_mutex_destroy(&session.flush_mutex), 0);
  EXPECT_EQ(played, submit);
  const auto statistics = session.statistics.snapshot();
  EXPECT_EQ(statistics.packets, 1);
  EXPECT_EQ(statistics.playNumber, (waitOnly ? 0 : 1));
  EXPECT_EQ(statistics.frames, (submit ? expectedFrames : 0));
  EXPECT_EQ(statistics.measurements, (submit && expectedFrames > 0 ? 1 : 0));
  EXPECT_EQ(session.statistics.sessionSummary(1000000000).hasObservedFrame, !waitOnly);
  EXPECT_EQ(prerollFrames, expectedPreroll);
  if (submit) {
    EXPECT_EQ(outputFrames, expectedFrames);
    if (expectedFrames > 0) {
      const int expected = mute ? 0 : 6000 * gain / 65536;
      EXPECT_NEAR(outputSample, expected, 1);
    }
  }
}

TEST(PlayerVolumeWait, NoDelayUsesUpdatedGainAndFullPacketLength) {
  checkPlayback({});
}

TEST(PlayerVolumeWait, UnderrunDelaySkipsFortyFourFramesEvenWhenSyncIsDisabled) {
  checkPlayback({.hasDelay = true, .expectedFrames = 308});
}

TEST(PlayerVolumeWait, ContinuingDiscardWaitsWithoutSubmittingCallback) {
  checkPlayback({.hasDelay = true, .frameTime = 991000000, .expectedFrames = 0, .submit = false});
}

TEST(PlayerVolumeWait, ExactDiscardSubmitsZeroFrames) {
  checkPlayback({.hasDelay = true, .frameTime = 992000000, .expectedFrames = 0});
}

TEST(PlayerVolumeWait, MuteSubmitsSilenceAtFullPacketLength) {
  checkPlayback({.mute = true});
}

TEST(PlayerVolumeWait, MissingAnchorWaitsWithoutPlayback) {
  checkPlayback({.expectedFrames = 0, .submit = false, .waitOnly = true, .anchor = false});
}

TEST(PlayerVolumeWait, ZeroFrameTimeWaitsWithoutPlayback) {
  checkPlayback({.frameTime = 0, .expectedFrames = 0, .submit = false, .waitOnly = true});
}

TEST(PlayerVolumeWait, NoDelayPrerollSubmitsSilenceThenWaits) {
  checkPlayback({.frameTime = 1150000000, .expectedFrames = 0, .submit = false,
                 .waitOnly = true, .expectedPreroll = 6615});
}

TEST(PlayerVolumeWait, PacketWaitsForArrivalPublicationBeforePreroll) {
  ArrivalPublicationProbe probe;
  arrivalPublicationProbe = &probe;
  checkPlayback({.frameTime = 1150000000, .expectedFrames = 0, .submit = false,
                 .waitOnly = true, .expectedPreroll = 6615});
  arrivalPublicationProbe = nullptr;
}

TEST(PlayerVolumeWait, DelayPrerollSubmitsSilenceThenWaits) {
  checkPlayback({.hasDelay = true, .frameTime = 1050000000, .expectedFrames = 0, .submit = false,
                 .waitOnly = true, .expectedPreroll = 2205});
}

TEST(PlayerVolumeWait, UnavailableFrameTimeConversionWaitsWithoutPlayback) {
  checkPlayback({.expectedFrames = 0, .submit = false, .waitOnly = true, .conversion = false});
}
