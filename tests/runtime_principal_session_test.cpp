#include "session/runtime_principal_session.hpp"
#include "cancellation_wait.hpp"
#include <gtest/gtest.h>
#include <chrono>
#include <future>

TEST(RuntimePrincipalSession, SnapshotCopiesPlaybackAndGroupMetadataFromSelectedSession) {
  CancellationWait playback;
  RuntimePrincipalSession principal;
  SessionState session{};
  session.connection_number = 31;
  session.airplay_stream_category = ptp_stream;
  session.inputAudio.setSetupSampleRate(48000);
  session.type = 103;
  char group[] = "group-one";
  session.airplay_gid = group;
  session.groupContainsGroupLeader = 1;
  EXPECT_FALSE(principal.snapshot().id);
  EXPECT_EQ(principal.withCurrent([](SessionState *current) { return current; }), nullptr);
  ASSERT_TRUE(principal.acquire(session, true).accepted);
  EXPECT_EQ(principal.withCurrent([](SessionState *current) { return current; }), &session);
  EXPECT_EQ(session.playbackRun.start([](void *argument) -> void * {
    static_cast<CancellationWait *>(argument)->block();
    return nullptr;
  }, &playback), PlaybackRun::StartResult::started);
  playback.waitForBlocked(1);
  const auto snapshot = principal.snapshot();
  EXPECT_EQ(snapshot.id, 31);
  EXPECT_TRUE(snapshot.playing);
  EXPECT_EQ(snapshot.category, ptp_stream);
  EXPECT_EQ(snapshot.inputRate, 48000u);
  EXPECT_EQ(snapshot.type, 103u);
  EXPECT_EQ(snapshot.groupId, "group-one");
  EXPECT_TRUE(snapshot.groupContainsLeader);
  session.playbackRun.stop();
  group[0] = 'G';
  EXPECT_EQ(snapshot.groupId, "group-one");
  EXPECT_TRUE(principal.releaseIfCurrent(31));
  const auto empty = principal.snapshot();
  EXPECT_FALSE(empty.id);
  EXPECT_FALSE(empty.playing);
  EXPECT_EQ(empty.category, unspecified_stream_category);
  EXPECT_EQ(empty.inputRate, 0u);
  EXPECT_EQ(empty.type, 0u);
  EXPECT_TRUE(empty.groupId.empty());
  EXPECT_FALSE(empty.groupContainsLeader);
}

TEST(RuntimePrincipalSession, GroupMutationAndSnapshotUseTheSelectionLock) {
  RuntimePrincipalSession principal;
  SessionState selected{}, other{};
  selected.connection_number = 32;
  other.connection_number = 33;
  ASSERT_TRUE(principal.acquire(selected, true).accepted);
  char group[] = "updated-group";
  std::promise<void> attempting;
  std::future<RuntimePrincipalSession::Snapshot> snapshot;
  EXPECT_EQ(principal.mutateSession(selected, [&](SessionState &session) {
    session.airplay_gid = group;
    snapshot = std::async(std::launch::async, [&] {
      attempting.set_value();
      return principal.snapshot();
    });
    attempting.get_future().wait();
    EXPECT_EQ(snapshot.wait_for(std::chrono::milliseconds(50)), std::future_status::timeout);
    session.groupContainsGroupLeader = 1;
    return 42;
  }), 42);
  const auto result = snapshot.get();
  EXPECT_EQ(result.groupId, "updated-group");
  EXPECT_TRUE(result.groupContainsLeader);
  principal.mutateSession(other, [](SessionState &session) { session.type = 105; });
  EXPECT_EQ(other.type, 105u);
  EXPECT_TRUE(principal.isCurrent(32));
  principal.releaseIfCurrent(32);
}
