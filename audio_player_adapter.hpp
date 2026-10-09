#pragma once
#include "audio_decoder.hpp"
#include "resampler.hpp"

struct SessionState;
void prepareIncomingAudio(SessionState &session, ssrc_t ssrc);
OwnedAudioFrame decodeIncomingAudio(SessionState &session, std::span<const uint8_t> bytes);
ConvertedAudio convertIncomingAudio(SessionState &session, const AVFrame &frame);
void encodePlaybackSample(int32_t sample, char **output, sps_format_t format, int gain,
                          int dither, SessionState *session);
void mutePlaybackPcm(char *output, size_t frames, uint32_t format, SessionState &session);
