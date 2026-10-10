#pragma once

#include "nqptp-utilities.h"
#include <cstddef>
#include <stop_token>
#include <string>
#include <sys/mman.h>
#include <signal.h>
#include <unistd.h>

namespace nqptp {

struct Endpoints {
  std::string ptp_address;
  std::string control_address = "localhost";
  uint16_t event_port = 319;
  uint16_t general_port = 320;
  uint16_t control_port = NQPTP_CONTROL_PORT;
  std::string shared_memory_name = NQPTP_INTERFACE_NAME;
  bool handle_signals = true;
};

struct MappingOperations {
  decltype(&::ftruncate) truncate = ::ftruncate;
  decltype(&::mmap) map = ::mmap;
};

class Runtime {
public:
  explicit Runtime(Endpoints endpoints = {}, MappingOperations mapping = {});
  ~Runtime();
  Runtime(const Runtime &) = delete;
  Runtime &operator=(const Runtime &) = delete;
  const Endpoints &endpoints() const { return endpoints_; }
  bool poll_once(int timeout_ms = 10);
  void run(std::stop_token stop = {});

private:
  Endpoints endpoints_;
  sockets_open_bundle sockets_{};
  int shared_memory_fd_ = -1;
  shm_structure *memory_ = nullptr;
  int signal_fd_ = -1;
  sigset_t previous_signal_mask_{};
  bool signal_mask_changed_ = false;
  bool shared_memory_owned_ = false;
  bool state_owned_ = false;
  uint64_t next_broadcast_ = 0;
  void bind_port(const std::string &address, uint16_t &port);
  void release() noexcept;
};

}
