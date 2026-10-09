#include "runtime_principal_session.hpp"
#include "volume_control.hpp"
#include <cassert>
#include <condition_variable>
#include <thread>

int main() {
  RuntimePrincipalSession principal;
  SessionState first{}, second{};
  first.connection_number = 1;
  second.connection_number = 2;
  SharedVolumeLevel shared;
  assert(principal.acquire(first, true).accepted);
  const auto firstTicket = principal.ticketFor(1);
  assert(firstTicket);
  assert(principal.snapshot().id == 1);
  assert(principal.acquire(second, true).accepted);
  assert(!principal.commitIfSelected(*firstTicket, [&] { shared.remember(-30); }));
  assert(shared.current() == -24);
  const auto secondTicket = principal.ticketFor(2);
  assert(principal.commitIfSelected(*secondTicket, [&] { shared.remember(-15); }));
  assert(shared.current() == -15);
  principal.releaseIfCurrent(2);
  assert(!principal.commitIfSelected(*secondTicket, [&] { shared.remember(0); }));

  RuntimePrincipalSession concurrent;
  SessionState oldSession{}, newSession{};
  oldSession.connection_number = 3;
  newSession.connection_number = 4;
  assert(concurrent.acquire(oldSession, true).accepted);
  std::mutex ordering;
  std::condition_variable changed;
  bool effectStarted = false, replacementSelected = false;
  std::thread oldEffect([&] {
    const auto ticket = concurrent.ticketFor(3);
    assert(ticket);
    assert(concurrent.snapshot().id == 3);
    {
      std::unique_lock lock(ordering);
      effectStarted = true;
      changed.notify_all();
      changed.wait(lock, [&] { return replacementSelected; });
    }
    assert(!concurrent.commitIfSelected(*ticket, [&] { shared.remember(-30); }));
  });
  {
    std::unique_lock lock(ordering);
    changed.wait(lock, [&] { return effectStarted; });
  }
  assert(concurrent.acquire(newSession, true).accepted);
  {
    std::lock_guard lock(ordering);
    replacementSelected = true;
  }
  changed.notify_all();
  oldEffect.join();
  const auto newTicket = concurrent.ticketFor(4);
  assert(concurrent.commitIfSelected(*newTicket, [&] { shared.remember(-12); }));
  assert(shared.current() == -12);
}
