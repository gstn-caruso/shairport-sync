#pragma once
#include "audio/decoding/audio_decoder.hpp"
#include "audio/resampling/resampler.hpp"
#include "audio/pcm/pcm_encoder.hpp"
#include "volume/volume_quantities.hpp"
#include <functional>

struct SessionState;
AirPlayVolume suggestedSessionVolume(const SessionState *session);
void applySessionVolume(AirPlayVolume level, SessionState &session);
void applySessionVolumeEffects(AirPlayVolume level, SessionState &session,
                               const std::function<void()> &publish = {});
void prepareIncomingAudio(SessionState &session, ssrc_t ssrc);
OwnedAudioFrame decodeIncomingAudio(SessionState &session, std::span<const uint8_t> bytes);
ConvertedAudio convertIncomingAudio(SessionState &session, const AVFrame &frame);
