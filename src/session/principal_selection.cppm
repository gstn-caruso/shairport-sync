module;
#include "session/principal_participant.hpp"
#include <cstdint>
#include <mutex>
#include <optional>

export module receiver.session.principal;

export class PrincipalSelection {
public:
  struct SelectionTicket { int id; std::uint64_t generation; };
  struct Acquisition {
    bool accepted;
    bool alreadyCurrent;
    std::optional<int> previousId;
  };
  Acquisition acquire(PrincipalParticipant &participant, bool allowReplacement);
  bool releaseIfCurrent(int id);
  std::optional<int> clear();
  bool isCurrent(int id) const;
  std::optional<SelectionTicket> ticketFor(int id) const;
  // Participants are borrowed until release. Actions run under the selection lock:
  // they must not reenter selection, join workers, or retain the participant pointer.
  template <typename Action> auto withCurrent(Action action) const {
    std::lock_guard lock(mutex_);
    return action(current_);
  }
  template <typename Action> bool commitIfSelected(SelectionTicket ticket, Action action) {
    std::lock_guard lock(mutex_);
    if (!current_ || current_->id() != ticket.id || generation_ != ticket.generation)
      return false;
    action();
    return true;
  }
private:
  mutable std::mutex mutex_;
  PrincipalParticipant *current_ = nullptr;
  std::uint64_t generation_ = 0;
};
