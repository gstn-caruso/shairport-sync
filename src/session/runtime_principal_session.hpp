#pragma once

#include "session/session_state.hpp"
#include <optional>
#include <string>
import receiver.session.principal;

class RuntimePrincipalSession {
public:
  using SelectionTicket = PrincipalSelection::SelectionTicket;
  using Acquisition = PrincipalSelection::Acquisition;
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
    return selection_.acquire(session, allowReplacement);
  }
  bool releaseIfCurrent(int id) { return selection_.releaseIfCurrent(id); }
  std::optional<int> clear() { return selection_.clear(); }
  bool isCurrent(int id) const { return selection_.isCurrent(id); }
  Snapshot snapshot() const {
    return selection_.withCurrent([](PrincipalParticipant *participant) -> Snapshot {
      const auto *current = static_cast<SessionState *>(participant);
      if (!current)
        return {};
      return {current->connection_number, current->playbackRun.isActive(),
              current->airplay_stream_category, current->inputAudio.sampleRate(), current->type,
              current->airplay_gid ? current->airplay_gid : "",
              current->groupContainsGroupLeader != 0};
    });
  }
  // Synchronous effect boundary: do not retain the borrowed session, join threads or reenter this
  // object from the action. Session teardown releases selection before freeing its resources.
  template <typename Action> auto withCurrent(Action action) {
    return selection_.withCurrent([&](PrincipalParticipant *participant) {
      return action(static_cast<SessionState *>(participant));
    });
  }
  std::optional<SelectionTicket> ticketFor(int id) const { return selection_.ticketFor(id); }
  template <typename Action> bool commitIfSelected(SelectionTicket ticket, Action action) {
    return selection_.commitIfSelected(ticket, action);
  }
  template <typename Action> auto mutateSession(SessionState &session, Action action) {
    return selection_.withCurrent([&](PrincipalParticipant *) { return action(session); });
  }

private:
  PrincipalSelection selection_;
};
