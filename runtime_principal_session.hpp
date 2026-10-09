#pragma once

#include "session_state.hpp"
#include <mutex>
#include <optional>
#include <string>

class RuntimePrincipalSession {
public:
  struct SelectionTicket { int id; uint64_t generation; };
  struct Acquisition {
    bool accepted;
    bool alreadyCurrent;
    std::optional<int> previousId;
  };
  struct Snapshot {
    std::optional<int> id;
    bool playing = false;
    airplay_stream_c category = unspecified_stream_category;
    unsigned int inputRate = 0;
    uint64_t type = 0;
    std::string groupId;
    bool groupContainsLeader = false;
  };
  Acquisition acquire(SessionState &session, bool allowReplacement) {
    std::lock_guard lock(mutex_);
    if (!session.mayAcquirePrincipal())
      return {false, false, {}};
    if (current_ == &session)
      return {true, true, {}};
    if (current_ && !allowReplacement)
      return {false, false, {}};
    auto previous = current_ ? std::optional(current_->connection_number) : std::nullopt;
    if (current_)
      current_->beginRetirement();
    current_ = &session;
    ++generation_;
    return {true, false, previous};
  }
  bool releaseIfCurrent(int id) {
    std::lock_guard lock(mutex_);
    if (!current_ || current_->connection_number != id)
      return false;
    current_ = nullptr;
    ++generation_;
    return true;
  }
  std::optional<int> clear() {
    std::lock_guard lock(mutex_);
    auto previous = current_ ? std::optional(current_->connection_number) : std::nullopt;
    if (current_)
      current_->beginRetirement();
    current_ = nullptr;
    ++generation_;
    return previous;
  }
  bool isCurrent(int id) const {
    std::lock_guard lock(mutex_);
    return current_ && current_->connection_number == id;
  }
  Snapshot snapshot() const {
    std::lock_guard lock(mutex_);
    if (!current_)
      return {};
    return {current_->connection_number, current_->is_playing != 0,
            current_->airplay_stream_category, current_->input_rate, current_->type,
            current_->airplay_gid ? current_->airplay_gid : "",
            current_->groupContainsGroupLeader != 0};
  }
  // Synchronous effect boundary: do not retain the borrowed session, join threads or reenter this
  // object from the action. Session teardown releases selection before freeing its resources.
  template <typename Action> auto withCurrent(Action action) {
    std::lock_guard lock(mutex_);
    return action(current_);
  }
  std::optional<SelectionTicket> ticketFor(int id) const {
    std::lock_guard lock(mutex_);
    if (!current_ || current_->connection_number != id)
      return std::nullopt;
    return SelectionTicket{id, generation_};
  }
  template <typename Action> bool commitIfSelected(SelectionTicket ticket, Action action) {
    std::lock_guard lock(mutex_);
    if (!current_ || current_->connection_number != ticket.id || generation_ != ticket.generation)
      return false;
    action();
    return true;
  }
  template <typename Action> bool applyIfCurrent(int id, Action action) {
    std::lock_guard lock(mutex_);
    if (!current_ || current_->connection_number != id) return false;
    action(*current_);
    return true;
  }
  template <typename Action> auto mutateSession(SessionState &session, Action action) {
    std::lock_guard lock(mutex_);
    return action(session);
  }

private:
  mutable std::mutex mutex_;
  SessionState *current_ = nullptr;
  uint64_t generation_ = 0;
};
