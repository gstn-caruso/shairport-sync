#include "session/principal_participant.hpp"
#include <gtest/gtest.h>
import receiver.session.principal;

namespace {
class Participant : public PrincipalParticipant {
public:
  explicit Participant(int identity) : identity_(identity) {}
  int id() const override { return identity_; }
  bool mayAcquirePrincipal() const override { return !retired; }
  void beginRetirement() override { retired = true; }
  bool retired = false;
private:
  int identity_;
};
}

TEST(PrincipalSelection, EmptySelectionHasNoTicketOrEffects) {
  PrincipalSelection selection;
  EXPECT_FALSE(selection.isCurrent(1));
  EXPECT_FALSE(selection.ticketFor(1));
  bool called = false;
  EXPECT_FALSE(selection.commitIfSelected({1, 0}, [&] { called = true; }));
  EXPECT_FALSE(called);
  EXPECT_FALSE(selection.clear());
  EXPECT_FALSE(selection.withCurrent([](PrincipalParticipant *current) { return current != nullptr; }));
}

TEST(PrincipalSelection, AdmissionPreservesIdentityAndRetiresDisplacedParticipants) {
  PrincipalSelection selection;
  Participant first(1), second(2), sameId(2);
  auto acquired = selection.acquire(first, false);
  ASSERT_TRUE(acquired.accepted);
  EXPECT_FALSE(acquired.alreadyCurrent);
  EXPECT_FALSE(acquired.previousId);
  auto ticket = selection.ticketFor(1);
  ASSERT_TRUE(ticket);
  auto repeated = selection.acquire(first, false);
  EXPECT_TRUE(repeated.accepted);
  EXPECT_TRUE(repeated.alreadyCurrent);
  EXPECT_EQ(selection.ticketFor(1)->generation, ticket->generation);
  EXPECT_FALSE(selection.acquire(second, false).accepted);
  EXPECT_TRUE(selection.isCurrent(1));
  EXPECT_FALSE(first.retired);
  acquired = selection.acquire(second, true);
  EXPECT_TRUE(acquired.accepted);
  EXPECT_EQ(acquired.previousId, 1);
  EXPECT_TRUE(first.retired);
  EXPECT_FALSE(selection.acquire(first, true).accepted);
  EXPECT_FALSE(selection.commitIfSelected(*ticket, [] {}));
  ticket = selection.ticketFor(2);
  ASSERT_TRUE(ticket);
  acquired = selection.acquire(sameId, true);
  EXPECT_TRUE(acquired.accepted);
  EXPECT_FALSE(acquired.alreadyCurrent);
  EXPECT_EQ(acquired.previousId, 2);
  EXPECT_TRUE(second.retired);
  EXPECT_FALSE(selection.commitIfSelected(*ticket, [] {}));
  sameId.beginRetirement();
  EXPECT_FALSE(selection.acquire(sameId, true).accepted);
}
