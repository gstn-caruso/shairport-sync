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
