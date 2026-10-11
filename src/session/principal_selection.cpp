module;
#include "session/principal_participant.hpp"
#include <mutex>
#include <optional>
module receiver.session.principal;

PrincipalSelection::Acquisition PrincipalSelection::acquire(PrincipalParticipant &, bool) {
  return {false, false, {}};
}
bool PrincipalSelection::releaseIfCurrent(int) { return false; }
std::optional<int> PrincipalSelection::clear() { return std::nullopt; }
bool PrincipalSelection::isCurrent(int id) const {
  std::lock_guard lock(mutex_);
  return current_ && current_->id() == id;
}
std::optional<PrincipalSelection::SelectionTicket> PrincipalSelection::ticketFor(int id) const {
  std::lock_guard lock(mutex_);
  if (!current_ || current_->id() != id)
    return std::nullopt;
  return SelectionTicket{id, generation_};
}
