#include "transport/exact_byte_input.hpp"
#include "protocol/ap2/buffered_block_input.hpp"
#include "buffered_block_fixture.hpp"
#include "audio/format/audio_format.hpp"
#include <gtest/gtest.h>
#include <optional>
#include <variant>
#include <string>
import receiver.protocol.ap2.buffered_coordinator;

struct CoordinatorInput : ExactByteInput {
  std::vector<uint8_t> bytes;
  size_t position = 0, reads = 0;
  ByteQueueStatus terminal = ByteQueueStatus::endOfStream;
  int error = 0;
  ByteQueueResult readExact(std::span<uint8_t> output) override {
    ++reads;
    const auto count = std::min(output.size(), bytes.size()-position);
    std::copy_n(bytes.begin()+position, count, output.begin());
    position += count;
    return {count == output.size() ? ByteQueueStatus::complete : terminal, count, bytes.size()-position, error};
  }
  void add(uint32_t timestamp = 1000, uint32_t ssrc = AAC_48000_F24_2, bool corrupt = false) {
    auto block = buffered_block_fixture::encrypt(buffered_block_fixture::plaintext, ssrc, timestamp);
    if (corrupt) block[12] ^= 1;
    bytes.push_back(0); bytes.push_back(block.size()+2);
    bytes.insert(bytes.end(), block.begin(), block.end());
  }
};
struct CoordinatorSession : BufferedSessionPort {
  std::vector<std::string> &effects;
  bool enabled = false, discard = false, keyPresent = true;
  mutable unsigned keyCalls = 0;
  std::vector<BufferedPacketMetadata> flushed;
  std::vector<bool> everRead;
  std::vector<BufferedReceiverDiagnostic> diagnostics;
  std::vector<BufferedBlockRead> observations;
  explicit CoordinatorSession(std::vector<std::string> &events) : effects(events) {}
  bool playbackEnabled() const override { effects.push_back("play"); return enabled; }
  bool evaluateFlush(bool read, const BufferedPacketMetadata &packet) override {
    effects.push_back("flush"); flushed.push_back(packet); everRead.push_back(read); return discard;
  }
  void observeRead(const BufferedBlockRead &read) override { effects.push_back("observe"); observations.push_back(read); }
  std::span<const uint8_t> key() const override { ++keyCalls; return keyPresent ? std::span(buffered_block_fixture::key) : std::span<const uint8_t>{}; }
  void diagnostic(const BufferedReceiverDiagnostic &event) override { diagnostics.push_back(event); }
};
struct CoordinatorClock : BufferedClockPort {
  std::vector<std::string> &effects;
  bool readyImmediately = true;
  unsigned readyChecks = 0, nowCalls = 0;
  std::optional<uint64_t> scheduled = 1050000000;
  explicit CoordinatorClock(std::vector<std::string> &events) : effects(events) {}
  bool ready() override { effects.push_back("ready"); return readyImmediately || ++readyChecks > 1; }
  std::optional<uint64_t> schedule(uint32_t timestamp) override { effects.push_back("map:"+std::to_string(timestamp)); return scheduled; }
  uint64_t now() override { ++nowCalls; effects.push_back("now"); return 1000000000; }
  void wait(unsigned duration) override { effects.push_back("wait:"+std::to_string(duration)); }
  void diagnoseAnchor() override { effects.push_back("anchor"); }
};
struct CoordinatorSink : BufferedAudioSinkPort {
  std::vector<std::string> &effects;
  BufferedInputShape input{};
  unsigned returned = 1024, initializations = 0, resets = 0;
  struct Sent { BufferedPacketMetadata packet; BufferedAudioSubmission submission; std::vector<uint8_t> bytes; };
  std::vector<Sent> sent;
  explicit CoordinatorSink(std::vector<std::string> &events) : effects(events) {}
  BufferedInputShape shape() const override { return input; }
  void initialize(uint32_t ssrc) override {
    effects.push_back("initialize"); ++initializations;
    auto format = *AudioFormat::fromSsrc(static_cast<ssrc_t>(ssrc));
    input = {format.framesPerPacket(),format.sampleRate()};
  }
  void reset() override { effects.push_back("reset"); ++resets; }
  unsigned submit(const BufferedPacketMetadata &packet, const BufferedAudioSubmission &submission,
                  std::span<uint8_t> payload) override {
    effects.push_back("submit"); sent.push_back({packet,submission,{payload.begin(),payload.end()}}); return returned;
  }
};
class BufferedCoordinator : public testing::Test {
protected:
  std::vector<std::string> effects;
  CoordinatorInput input;
  CoordinatorSession session{effects};
  CoordinatorClock clock{effects};
  CoordinatorSink sink{effects};
  template<class Diagnostic> std::vector<Diagnostic> diagnostics() const {
    std::vector<Diagnostic> result;
    for (const auto &event : session.diagnostics)
      if (auto value = std::get_if<Diagnostic>(&event)) result.push_back(*value);
    return result;
  }
};
TEST_F(BufferedCoordinator, FirstAdvanceWaitsForTimingResetsAndFlushesBeforeAnyBlock) {
  clock.readyImmediately = false;
  clock.scheduled = std::nullopt;
  BufferedReceiverCoordinator coordinator(input,session,clock,sink,0.25);
  EXPECT_EQ(coordinator.advance(), BufferedReceiverResult::continued);
  EXPECT_EQ(input.reads, 0u);
  ASSERT_EQ(session.everRead.size(), 1u);
  EXPECT_FALSE(session.everRead[0]);
  EXPECT_EQ(effects, (std::vector<std::string>{"ready","wait:1000","ready","reset","play","flush","map:0","wait:20000"}));
}
TEST_F(BufferedCoordinator, LaterBlockPreservesMetadataHistoryWithoutReinitializingInput) {
  session.enabled = true;
  input.add(1000); input.add(3000, ALAC_44100_S16_2);
  BufferedReceiverCoordinator coordinator(input,session,clock,sink,0.25);
  coordinator.advance(); coordinator.advance();
  ASSERT_EQ(sink.sent.size(), 2u);
  EXPECT_EQ(sink.initializations, 1u);
  EXPECT_EQ(sink.sent[1].submission.sequence, 0xcdf0);
  EXPECT_FALSE(sink.sent[1].submission.mute);
  EXPECT_EQ(sink.sent[1].submission.gap, 976);
  EXPECT_EQ(sink.sent[1].bytes, (std::vector<uint8_t>(buffered_block_fixture::plaintext.begin(),buffered_block_fixture::plaintext.end())));
  auto formats = diagnostics<BufferedFormatDiagnostic>();
  ASSERT_EQ(formats.size(), 3u);
  EXPECT_EQ(formats[0].kind, BufferedFormatKind::changed);
  EXPECT_FALSE(formats[0].switching);
  EXPECT_EQ(formats[1].kind, BufferedFormatKind::initial);
  EXPECT_EQ(formats[2].packet.ssrc, ALAC_44100_S16_2);
  EXPECT_TRUE(formats[2].switching);
  auto history = diagnostics<BufferedHistoryDiagnostic>();
  ASSERT_EQ(history.size(), 2u);
  EXPECT_EQ(history[0].kind, BufferedHistoryKind::sequence);
  EXPECT_EQ(history[0].expected, 0x2bcdf0u);
  EXPECT_EQ(history[0].previous, 0x2bcdefu);
  EXPECT_EQ(history[1].kind, BufferedHistoryKind::timestamp);
  EXPECT_EQ(history[1].expected, 2024u);
  EXPECT_EQ(history[1].packet.timestamp, 3000u);
}
TEST_F(BufferedCoordinator, CompleteEncryptedCycleObservesReadsInitializesFlushesAndSubmitsOwnedPayload) {
  input.add(); session.enabled = true;
  BufferedReceiverCoordinator coordinator(input,session,clock,sink,0.25);
  EXPECT_EQ(coordinator.advance(), BufferedReceiverResult::continued);
  EXPECT_EQ(input.reads, 2u);
  ASSERT_EQ(sink.sent.size(), 1u);
  EXPECT_EQ(sink.sent[0].packet.timestamp, 1000u);
  EXPECT_EQ(sink.sent[0].submission.sequence, 0xcdef);
  EXPECT_TRUE(sink.sent[0].submission.mute);
  EXPECT_EQ(sink.sent[0].submission.gap, 0);
  ASSERT_EQ(sink.sent[0].bytes.size(), buffered_block_fixture::plaintext.size()+7);
  EXPECT_TRUE(std::equal(buffered_block_fixture::plaintext.begin(), buffered_block_fixture::plaintext.end(), sink.sent[0].bytes.begin()+7));
  ASSERT_EQ(session.observations.size(), 1u);
  EXPECT_EQ(session.observations[0].prefixRemaining, 52u);
  EXPECT_EQ(session.observations[0].bodyRemaining, 0u);
  EXPECT_TRUE(session.everRead[0]);
  EXPECT_EQ(effects, (std::vector<std::string>{"ready","reset","play","observe","initialize","flush","map:1000","now","submit"}));
}
TEST_F(BufferedCoordinator, CachedClockAndEarlyWaitsRetainStableWireWithoutRereadOrAuthentication) {
  input.add(); session.enabled = true; clock.scheduled = std::nullopt;
  BufferedReceiverCoordinator coordinator(input,session,clock,sink,0.25);
  coordinator.advance();
  EXPECT_EQ(input.reads, 2u); EXPECT_EQ(clock.nowCalls, 0u); EXPECT_EQ(session.keyCalls, 0u);
  std::fill(input.bytes.begin(),input.bytes.end(),0);
  clock.scheduled = 1500000000;
  coordinator.advance(); coordinator.advance();
  EXPECT_EQ(input.reads, 2u); EXPECT_EQ(session.keyCalls, 0u);
  auto admissions = diagnostics<BufferedAdmissionDiagnostic>();
  ASSERT_EQ(admissions.size(), 2u);
  EXPECT_EQ(admissions[0].kind, BufferedAdmissionDiagnosticKind::clockWait);
  EXPECT_EQ(admissions[1].kind, BufferedAdmissionDiagnosticKind::early);
  clock.scheduled = 1050000000;
  coordinator.advance();
  ASSERT_EQ(sink.sent.size(), 1u);
  EXPECT_EQ(input.reads, 2u);
  EXPECT_TRUE(std::equal(buffered_block_fixture::plaintext.begin(),buffered_block_fixture::plaintext.end(),sink.sent[0].bytes.begin()+7));
  EXPECT_EQ(session.everRead.size(), 4u);
}
TEST_F(BufferedCoordinator, DisabledCachedBlockWaitsButRestartRequiresFreshInput) {
  input.add(1000); input.add(2024); session.enabled = true; clock.scheduled = std::nullopt;
  BufferedReceiverCoordinator coordinator(input,session,clock,sink,0.25);
  coordinator.advance();
  session.enabled = false;
  coordinator.advance();
  EXPECT_EQ(input.reads, 2u); EXPECT_EQ(sink.resets, 2u);
  session.enabled = true; clock.scheduled = 1050000000;
  coordinator.advance();
  EXPECT_EQ(input.reads, 4u);
  ASSERT_EQ(sink.sent.size(), 1u);
  EXPECT_EQ(sink.sent[0].packet.timestamp, 2024u);
  EXPECT_TRUE(sink.sent[0].submission.mute);
}
TEST_F(BufferedCoordinator, FlushCachedBlockRunsBeforeAdmissionAndConsumesWithoutAuthentication) {
  input.add(1000); input.add(2024); session.enabled = true; clock.scheduled = std::nullopt;
  BufferedReceiverCoordinator coordinator(input,session,clock,sink,0.25);
  coordinator.advance(); effects.clear();
  session.discard = true;
  coordinator.advance();
  EXPECT_EQ(effects, (std::vector<std::string>{"play","flush"}));
  EXPECT_EQ(input.reads, 2u); EXPECT_EQ(session.keyCalls, 0u);
  session.discard = false; clock.scheduled = 1050000000;
  coordinator.advance();
  ASSERT_EQ(sink.sent.size(), 1u);
  EXPECT_EQ(sink.sent[0].packet.timestamp, 2024u);
}
