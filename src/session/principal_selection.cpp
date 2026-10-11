module;
#include "session/principal_participant.hpp"
#include <mutex>
#include <optional>
module receiver.session.principal;

PrincipalSelection::Acquisition PrincipalSelection::acquire(PrincipalParticipant &participant,
                                                          bool allowReplacement) {
  std::lock_guard lock(mutex_);
  if (!participant.mayAcquirePrincipal())
    return {false, false, {}};
  if (current_ == &participant)
    return {true, true, {}};
  if (current_ && !allowReplacement)
    return {false, false, {}};
  auto previous = current_ ? std::optional(current_->id()) : std::nullopt;
  if (current_)
    current_->beginRetirement();
  current_ = &participant;
  ++generation_;
  return {true, false, previous};
}
bool PrincipalSelection::releaseIfCurrent(int id) {
  std::lock_guard lock(mutex_);
  if (!current_ || current_->id() != id)
    return false;
  current_ = nullptr;
  ++generation_;
  return true;
}
std::optional<int> PrincipalSelection::clear() {
  std::lock_guard lock(mutex_);
  auto previous = current_ ? std::optional(current_->id()) : std::nullopt;
  if (current_)
    current_->beginRetirement();
  current_ = nullptr;
  ++generation_;
  return previous;
}
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
