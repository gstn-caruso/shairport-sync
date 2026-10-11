#pragma once

#include "session/session_category.h"

class ManagedSession {
public:
  // start() must not synchronously call back into its registry. Successful admission transfers
  // ownership until join() returns; liveCategory() reflects the worker's current category.
  virtual ~ManagedSession() = default;
  virtual int id() const = 0;
  virtual airplay_stream_c liveCategory() const = 0;
  virtual int start() = 0;
  virtual void requestStop() = 0;
  virtual void join() = 0;
};
