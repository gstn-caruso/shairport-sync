#include "buffered_block_fixture.hpp"
#include <gtest/gtest.h>
#include <span>
#include <cstdint>
import receiver.protocol.ap2.realtime_audio;

TEST(RealtimeEncryptedAudio, GoldenStrippedHeaderAuthenticatesMetadataAndOwnsPlaintext) {
  auto wire = buffered_block_fixture::encrypt();
  auto result = RealtimeEncryptedAudio::decode(std::span(wire).subspan(2),buffered_block_fixture::key);
  ASSERT_TRUE(result);
  EXPECT_EQ(result->sequence,0xcdef);
  EXPECT_EQ(result->timestamp,0x01020304u);
  std::fill(wire.begin(),wire.end(),0);
  EXPECT_EQ(result->plaintext,(std::vector<uint8_t>(buffered_block_fixture::plaintext.begin(),buffered_block_fixture::plaintext.end())));
}
TEST(RealtimeEncryptedAudio, AllShortPacketsAndOversizeRejectBeforeHeaderReads) {
  for (size_t length = 0; length < 34; ++length) {
    SCOPED_TRACE(length);
    std::vector<uint8_t> bytes(length);
    EXPECT_EQ(RealtimeEncryptedAudio::decode(bytes,buffered_block_fixture::key).error(),RealtimeAudioError::tooShort);
  }
  std::vector<uint8_t> bytes(4095);
  EXPECT_EQ(RealtimeEncryptedAudio::decode(bytes,buffered_block_fixture::key).error(),RealtimeAudioError::tooLarge);
}
TEST(RealtimeEncryptedAudio, ExactMaximumAndAuthenticatedEmptyPlaintextAreValid) {
  std::vector<uint8_t> payload(4060);
  for (size_t index = 0; index < payload.size(); ++index) payload[index] = index % 251;
  auto wire = buffered_block_fixture::encrypt(payload);
  auto maximum = RealtimeEncryptedAudio::decode(std::span(wire).subspan(2),buffered_block_fixture::key);
  ASSERT_TRUE(maximum); EXPECT_EQ(maximum->plaintext,payload);
  wire = buffered_block_fixture::encrypt(std::span<const uint8_t>{});
  ASSERT_EQ(wire.size()-2,34u);
  auto empty = RealtimeEncryptedAudio::decode(std::span(wire).subspan(2),buffered_block_fixture::key);
  ASSERT_TRUE(empty); EXPECT_TRUE(empty->plaintext.empty()); EXPECT_EQ(empty->sequence,0xcdef);
}
TEST(RealtimeEncryptedAudio, MissingWrongLengthAndWrongValueKeysHaveExplicitErrors) {
  auto wire = buffered_block_fixture::encrypt();
  const auto stripped = std::span(wire).subspan(2);
  EXPECT_EQ(RealtimeEncryptedAudio::decode(stripped,{}).error(),RealtimeAudioError::missingKey);
  for (size_t length : {1u,31u,33u}) {
    std::vector<uint8_t> key(length);
    EXPECT_EQ(RealtimeEncryptedAudio::decode(stripped,key).error(),RealtimeAudioError::invalidKeySize);
  }
  auto key = buffered_block_fixture::key; key[0] ^= 1;
  EXPECT_EQ(RealtimeEncryptedAudio::decode(stripped,key).error(),RealtimeAudioError::authenticationFailed);
}
TEST(RealtimeEncryptedAudio, SequenceOutsideAadCanChangeWithoutInvalidatingAuthentication) {
  auto wire = buffered_block_fixture::encrypt(); wire[2] = 0xff; wire[3] = 0xff;
  auto result = RealtimeEncryptedAudio::decode(std::span(wire).subspan(2),buffered_block_fixture::key);
  ASSERT_TRUE(result); EXPECT_EQ(result->sequence,0xffff);
}
struct RealtimeTamper { const char *name; size_t offset; };
void PrintTo(const RealtimeTamper &scenario,std::ostream *output) { *output << scenario.name; }
class RealtimeAuthentication : public testing::TestWithParam<RealtimeTamper> {};
TEST_P(RealtimeAuthentication, SeparateAuthenticatedFieldsRejectMutation) {
  auto wire = buffered_block_fixture::encrypt(); wire[GetParam().offset] ^= 1;
  EXPECT_EQ(RealtimeEncryptedAudio::decode(std::span(wire).subspan(2),buffered_block_fixture::key).error(),RealtimeAudioError::authenticationFailed);
}
INSTANTIATE_TEST_SUITE_P(Fields,RealtimeAuthentication,
    testing::Values(RealtimeTamper{"Timestamp",4},RealtimeTamper{"Ssrc",8},RealtimeTamper{"Ciphertext",12},RealtimeTamper{"Tag",28},RealtimeTamper{"Nonce",51}),
    [](const auto &scenario) { return scenario.param.name; });
struct RealtimeDatagrams : DatagramInput {
  struct Packet { std::vector<uint8_t> bytes; int error = 0; };
  std::vector<Packet> packets;
  size_t next = 0;
  DatagramRead read(std::span<uint8_t> destination) override {
    const auto &packet = packets.at(next++);
    std::copy(packet.bytes.begin(),packet.bytes.end(),destination.begin());
    return {packet.bytes.size(),packet.error};
  }
};
struct RealtimeDraws : RandomSource {
  double value = 0;
  unsigned calls = 0;
  double draw() override { ++calls; return value; }
};
TEST(RealUDPIngress, ErrorAndStrictThirtySixByteBoundaryDoNotDrawRandomNumbers) {
  RealtimeDatagrams input;
  input.packets = {{{},42},{buffered_block_fixture::encrypt(std::span<const uint8_t>{}),0},{std::vector<uint8_t>(35),0},
                  {buffered_block_fixture::encrypt(std::span(buffered_block_fixture::plaintext).first(1)),0}};
  RealtimeDraws random;
  RealUDPIngress ingress(input,random,0.5);
  auto error = ingress.one(buffered_block_fixture::key);
  EXPECT_EQ(error.kind,RealtimeReceiveKind::readError); EXPECT_EQ(error.errorCode,42);
  EXPECT_EQ(ingress.one(buffered_block_fixture::key).kind,RealtimeReceiveKind::shortPacket);
  EXPECT_EQ(ingress.one(buffered_block_fixture::key).kind,RealtimeReceiveKind::shortPacket);
  EXPECT_EQ(random.calls,0u);
  random.value = 1;
  auto minimal = ingress.one(buffered_block_fixture::key);
  ASSERT_EQ(minimal.kind,RealtimeReceiveKind::audio);
  ASSERT_TRUE(minimal.audio);
  EXPECT_EQ(minimal.audio->plaintext,(std::vector<uint8_t>{0x10}));
  EXPECT_EQ(random.calls,1u);
}
TEST(RealUDPIngress, ZeroFractionBypassesRandomAndConsecutiveDatagramsOwnPayloadAcrossSequenceWrap) {
  RealtimeDatagrams input;
  auto first = buffered_block_fixture::encrypt(); first[2] = 0xff; first[3] = 0xff;
  auto second = buffered_block_fixture::encrypt(); second[2] = 0; second[3] = 0;
  input.packets = {{first,0},{second,0}};
  RealtimeDraws random;
  RealUDPIngress ingress(input,random,0);
  auto one = ingress.one(buffered_block_fixture::key);
  auto two = ingress.one(buffered_block_fixture::key);
  ASSERT_EQ(one.kind,RealtimeReceiveKind::audio); ASSERT_TRUE(one.audio);
  ASSERT_EQ(two.kind,RealtimeReceiveKind::audio); ASSERT_TRUE(two.audio);
  EXPECT_EQ(one.audio->sequence,0xffff); EXPECT_EQ(two.audio->sequence,0);
  EXPECT_EQ(one.audio->plaintext,two.audio->plaintext);
  EXPECT_EQ(random.calls,0u);
}
TEST(RealUDPIngress, EqualityDropsAndOnlyStrictlyGreaterRandomDrawAdmits) {
  RealtimeDatagrams input;
  const auto wire = buffered_block_fixture::encrypt(); input.packets = {{wire,0},{wire,0},{wire,0}};
  RealtimeDraws random;
  RealUDPIngress ingress(input,random,0.5);
  random.value = 0.5; EXPECT_EQ(ingress.one(buffered_block_fixture::key).kind,RealtimeReceiveKind::dropped);
  random.value = 0.500001; EXPECT_EQ(ingress.one(buffered_block_fixture::key).kind,RealtimeReceiveKind::audio);
  random.value = 0.499999; EXPECT_EQ(ingress.one(buffered_block_fixture::key).kind,RealtimeReceiveKind::dropped);
  EXPECT_EQ(random.calls,3u);
}
TEST(RealUDPIngress, AuthenticationAndMissingKeyReturnTypedRejectionsWithoutAudio) {
  RealtimeDatagrams input;
  auto bad = buffered_block_fixture::encrypt(); bad[12] ^= 1;
  input.packets = {{bad,0},{buffered_block_fixture::encrypt(),0}};
  RealtimeDraws random;
  RealUDPIngress ingress(input,random,0);
  auto failure = ingress.one(buffered_block_fixture::key);
  EXPECT_EQ(failure.kind,RealtimeReceiveKind::rejected);
  EXPECT_EQ(failure.rejection,RealtimeAudioError::authenticationFailed); EXPECT_FALSE(failure.audio);
  auto missing = ingress.one({});
  EXPECT_EQ(missing.rejection,RealtimeAudioError::missingKey); EXPECT_FALSE(missing.audio);
}
