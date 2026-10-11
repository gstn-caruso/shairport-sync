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
class RealtimeAuthentication : public testing::TestWithParam<RealtimeTamper> {};
TEST_P(RealtimeAuthentication, SeparateAuthenticatedFieldsRejectMutation) {
  auto wire = buffered_block_fixture::encrypt(); wire[GetParam().offset] ^= 1;
  EXPECT_EQ(RealtimeEncryptedAudio::decode(std::span(wire).subspan(2),buffered_block_fixture::key).error(),RealtimeAudioError::authenticationFailed);
}
INSTANTIATE_TEST_SUITE_P(Fields,RealtimeAuthentication,
    testing::Values(RealtimeTamper{"Timestamp",4},RealtimeTamper{"Ssrc",8},RealtimeTamper{"Ciphertext",12},RealtimeTamper{"Tag",28},RealtimeTamper{"Nonce",51}),
    [](const auto &scenario) { return scenario.param.name; });
