#include "session/runtime_principal_session.hpp"
#include "volume/volume_control.hpp"
#include <gtest/gtest.h>
#include <condition_variable>
#include <thread>

TEST(PrincipalVolume, RetiredTicketsCannotCommitSharedVolumeEffects) {
  SharedVolumeLevel shared;
  RuntimePrincipalSession principal;
  SessionState first{}, second{};
  first.connection_number = 1;
  second.connection_number = 2;
  ASSERT_TRUE(principal.acquire(first, true).accepted);
  const auto firstTicket = principal.ticketFor(1);
  ASSERT_TRUE(firstTicket);
  EXPECT_EQ(principal.snapshot().id, 1);
  ASSERT_TRUE(principal.acquire(second, true).accepted);
  EXPECT_FALSE(principal.commitIfSelected(*firstTicket, [&] { shared.remember(AirPlayVolume{-30}); }));
  EXPECT_EQ(shared.current(), AirPlayVolume{-24});
  const auto secondTicket = principal.ticketFor(2);
  ASSERT_TRUE(secondTicket);
  EXPECT_TRUE(principal.commitIfSelected(*secondTicket, [&] { shared.remember(AirPlayVolume{-15}); }));
  EXPECT_EQ(shared.current(), AirPlayVolume{-15});
  principal.releaseIfCurrent(2);
  EXPECT_FALSE(principal.commitIfSelected(*secondTicket, [&] { shared.remember(AirPlayVolume{0}); }));
}

TEST(PrincipalVolume, ConcurrentReplacementRejectsOldEffectAndCommitsNewLevel) {
  SharedVolumeLevel shared(AirPlayVolume{-15});
  RuntimePrincipalSession concurrent;
  SessionState oldSession{}, newSession{};
  oldSession.connection_number = 3;
  newSession.connection_number = 4;
  ASSERT_TRUE(concurrent.acquire(oldSession, true).accepted);
  std::mutex ordering;
  std::condition_variable changed;
  bool effectStarted = false, replacementSelected = false;
  std::thread oldEffect([&] {
    const auto ticket = concurrent.ticketFor(3);
    EXPECT_TRUE(ticket);
    EXPECT_EQ(concurrent.snapshot().id, 3);
    {
      std::unique_lock lock(ordering);
      effectStarted = true;
      changed.notify_all();
      changed.wait(lock, [&] { return replacementSelected; });
    }
    if (ticket)
      EXPECT_FALSE(concurrent.commitIfSelected(*ticket, [&] { shared.remember(AirPlayVolume{-30}); }));
  });
  {
    std::unique_lock lock(ordering);
    changed.wait(lock, [&] { return effectStarted; });
  }
  EXPECT_TRUE(concurrent.acquire(newSession, true).accepted);
  {
    std::lock_guard lock(ordering);
    replacementSelected = true;
  }
  changed.notify_all();
  oldEffect.join();
  const auto newTicket = concurrent.ticketFor(4);
  ASSERT_TRUE(newTicket);
  EXPECT_TRUE(concurrent.commitIfSelected(*newTicket, [&] { shared.remember(AirPlayVolume{-12}); }));
  EXPECT_EQ(shared.current(), AirPlayVolume{-12});
}
