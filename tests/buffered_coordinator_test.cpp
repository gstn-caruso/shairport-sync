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
  std::span<const uint8_t> key() const override { return keyPresent ? std::span(buffered_block_fixture::key) : std::span<const uint8_t>{}; }
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
