#include "session/session_registry.hpp"
#include <gtest/gtest.h>
#include <cerrno>
#include <string>
#include <vector>

namespace {
struct WorkerState {
  int id;
  airplay_stream_c category = unspecified_stream_category;
  int startResult = 0;
  std::vector<std::string> &events;
};

class TestSession final : public ManagedSession {
public:
  explicit TestSession(WorkerState &state) : state_(state) {}
  ~TestSession() override { record("destroy"); }
  int id() const override { return state_.id; }
  airplay_stream_c liveCategory() const override { return state_.category; }
  int start() override {
    record("start");
    return state_.startResult;
  }
  void requestStop() override { record("stop"); }
  void join() override { record("join"); }

private:
  void record(const char *operation) {
    state_.events.push_back(std::string(operation) + std::to_string(state_.id));
  }
  WorkerState &state_;
};
}

TEST(ManagedSessionRegistry, FailedStartDestroysOwnedWorkerWithoutJoining) {
  std::vector<std::string> events;
  WorkerState worker{1, ptp_stream, EAGAIN, events};
  SessionRegistry registry;
  EXPECT_EQ(registry.start(std::make_unique<TestSession>(worker)), EAGAIN);
  EXPECT_FALSE(registry.cancelAndJoin(1));
  registry.shutdown();
  EXPECT_EQ(events, (std::vector<std::string>{"start1", "destroy1"}));
}

TEST(ManagedSessionRegistry, ClosedRegistryDestroysAdmissionWithoutStarting) {
  std::vector<std::string> events;
  WorkerState worker{1, ptp_stream, 0, events};
  SessionRegistry registry;
  registry.shutdown();
  EXPECT_EQ(registry.start(std::make_unique<TestSession>(worker)), ECANCELED);
  EXPECT_EQ(events, (std::vector<std::string>{"destroy1"}));
}

TEST(ManagedSessionRegistry, CompletedWorkerJoinsOnceWithoutStopDespiteDuplicateFinish) {
  std::vector<std::string> events;
  WorkerState worker{1, ptp_stream, 0, events};
  SessionRegistry registry;
  ASSERT_EQ(registry.start(std::make_unique<TestSession>(worker)), 0);
  registry.markFinished(99);
  registry.markFinished(1);
  registry.markFinished(1);
  registry.joinFinished();
  registry.markFinished(1);
  registry.joinFinished();
  EXPECT_FALSE(registry.cancelAndJoin(1));
  EXPECT_EQ(events, (std::vector<std::string>{"start1", "join1", "destroy1"}));
}

TEST(ManagedSessionRegistry, MatchingUsesLiveCategoryAndPreservesExcludedWorker) {
  std::vector<std::string> events;
  WorkerState first{1, remote_control_stream, 0, events};
  WorkerState excluded{2, ptp_stream, 0, events};
  WorkerState other{3, remote_control_stream, 0, events};
  SessionRegistry registry;
  ASSERT_EQ(registry.start(std::make_unique<TestSession>(first)), 0);
  ASSERT_EQ(registry.start(std::make_unique<TestSession>(excluded)), 0);
  ASSERT_EQ(registry.start(std::make_unique<TestSession>(other)), 0);
  first.category = ptp_stream;
  events.clear();
  registry.cancelAndJoinMatching(ptp_stream, 2);
  EXPECT_EQ(events, (std::vector<std::string>{"stop1", "join1", "destroy1"}));
  EXPECT_FALSE(registry.cancelAndJoin(1));
  EXPECT_TRUE(registry.cancelAndJoin(2));
  EXPECT_TRUE(registry.cancelAndJoin(3));
}

TEST(ManagedSessionRegistry, EverySelectedWorkerReceivesStopBeforeAnyJoin) {
  std::vector<std::string> events;
  WorkerState first{1, ptp_stream, 0, events};
  WorkerState second{2, remote_control_stream, 0, events};
  SessionRegistry registry;
  ASSERT_EQ(registry.start(std::make_unique<TestSession>(first)), 0);
  ASSERT_EQ(registry.start(std::make_unique<TestSession>(second)), 0);
  events.clear();
  registry.cancelAndJoinMatching(unspecified_stream_category, 0);
  EXPECT_EQ(events, (std::vector<std::string>{"stop1", "stop2", "join1", "join2",
                                             "destroy1", "destroy2"}));
}

TEST(ManagedSessionRegistry, RepeatedShutdownAndDestructionRetireWorkersOnce) {
  std::vector<std::string> events;
  WorkerState first{1, ptp_stream, 0, events};
  WorkerState second{2, ptp_stream, 0, events};
  {
    SessionRegistry registry;
    ASSERT_EQ(registry.start(std::make_unique<TestSession>(first)), 0);
    ASSERT_EQ(registry.start(std::make_unique<TestSession>(second)), 0);
    events.clear();
    registry.shutdown();
    registry.shutdown();
  }
  EXPECT_EQ(events, (std::vector<std::string>{"stop1", "stop2", "join1", "join2",
                                             "destroy1", "destroy2"}));
}

TEST(ManagedSessionRegistry, DestructionJoinsBeforeReleasingWorkerOwnership) {
  std::vector<std::string> events;
  WorkerState worker{1, ptp_stream, 0, events};
  {
    SessionRegistry registry;
    ASSERT_EQ(registry.start(std::make_unique<TestSession>(worker)), 0);
  }
  EXPECT_EQ(events, (std::vector<std::string>{"start1", "stop1", "join1", "destroy1"}));
}
