#include "runtime.hpp"
#include <gtest/gtest.h>
#include <arpa/inet.h>
#include <array>
#include <cerrno>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <system_error>

namespace {
class Descriptor {
public:
  explicit Descriptor(int fd) : fd_(fd) {}
  ~Descriptor() { if (fd_ >= 0) close(fd_); }
  Descriptor(const Descriptor &) = delete;
  int get() const { return fd_; }
private:
  int fd_;
};

int bind_udp(uint16_t port, const char *address = "127.0.0.2") {
  const int fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
  if (fd < 0) throw std::system_error(errno, std::generic_category());
  sockaddr_in local{};
  local.sin_family = AF_INET;
  local.sin_port = htons(port);
  inet_pton(AF_INET, address, &local.sin_addr);
  if (bind(fd, reinterpret_cast<sockaddr *>(&local), sizeof(local)) < 0) {
    const int error = errno;
    close(fd);
    throw std::system_error(error, std::generic_category());
  }
  return fd;
}

uint16_t bound_port(int fd) {
  sockaddr_in local{};
  socklen_t length = sizeof(local);
  if (getsockname(fd, reinterpret_cast<sockaddr *>(&local), &length) < 0)
    throw std::system_error(errno, std::generic_category());
  return ntohs(local.sin_port);
}

class NqptpRuntime : public ::testing::Test {
protected:
  nqptp::Endpoints settings;
  void SetUp() override {
    settings.ptp_address = "127.0.0.2";
    settings.control_address = "127.0.0.2";
    settings.handle_signals = false;
    settings.shared_memory_name = "/shairport-nqptp-test-" + std::to_string(getpid());
    Descriptor event(bind_udp(0)), general(bind_udp(0)), control(bind_udp(0));
    settings.event_port = bound_port(event.get());
    settings.general_port = bound_port(general.get());
    settings.control_port = bound_port(control.get());
  }
  void TearDown() override { shm_unlink(settings.shared_memory_name.c_str()); }
  void expectPortsReleased() {
    EXPECT_NO_THROW({ Descriptor event(bind_udp(settings.event_port)); });
    EXPECT_NO_THROW({ Descriptor general(bind_udp(settings.general_port)); });
    EXPECT_NO_THROW({ Descriptor control(bind_udp(settings.control_port)); });
  }
  void expectMemoryRemoved() {
    const int fd = shm_open(settings.shared_memory_name.c_str(), O_RDONLY, 0);
    EXPECT_EQ(fd, -1);
    if (fd >= 0) close(fd);
    else EXPECT_EQ(errno, ENOENT);
  }
};

TEST_F(NqptpRuntime, ReleasesEarlierBindingsWhenAnotherRequiredPortIsOccupied) {
  { Descriptor conflict(bind_udp(settings.general_port));
    EXPECT_THROW({ nqptp::Runtime runtime(settings); }, std::system_error);
    EXPECT_NO_THROW({ Descriptor event(bind_udp(settings.event_port)); });
  }
  expectPortsReleased();
  expectMemoryRemoved();
}

TEST_F(NqptpRuntime, PreservesAnExistingSharedMemoryObjectOnStartupConflict) {
  Descriptor existing(shm_open(settings.shared_memory_name.c_str(), O_CREAT | O_EXCL | O_RDWR, 0600));
  ASSERT_GE(existing.get(), 0);
  constexpr uint32_t marker = 0x12345678;
  ASSERT_EQ(write(existing.get(), &marker, sizeof(marker)), sizeof(marker));
  EXPECT_THROW({ nqptp::Runtime runtime(settings); }, std::system_error);
  uint32_t observed{};
  ASSERT_EQ(pread(existing.get(), &observed, sizeof(observed), 0), sizeof(observed));
  EXPECT_EQ(observed, marker);
  expectPortsReleased();
}

TEST_F(NqptpRuntime, IPv4ConflictIsFatalEvenWhenIPv6CouldBind) {
  settings.ptp_address = "localhost";
  Descriptor conflict(bind_udp(settings.general_port, "127.0.0.1"));
  EXPECT_THROW({ nqptp::Runtime runtime(settings); }, std::system_error);
  EXPECT_NO_THROW({ Descriptor released(bind_udp(settings.event_port, "127.0.0.1")); });
  expectMemoryRemoved();
}

TEST_F(NqptpRuntime, IPv6ConflictIsFatalEvenWhenIPv4CouldBind) {
  const int fd = socket(AF_INET6, SOCK_DGRAM | SOCK_CLOEXEC, 0);
  if (fd < 0 && (errno == EAFNOSUPPORT || errno == EPROTONOSUPPORT))
    GTEST_SKIP() << "Kernel has no IPv6 support";
  Descriptor conflict(fd);
  ASSERT_GE(fd, 0);
  sockaddr_in6 address{};
  address.sin6_family = AF_INET6;
  inet_pton(AF_INET6, "::1", &address.sin6_addr);
  ASSERT_EQ(bind(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)), 0);
  socklen_t length = sizeof(address);
  ASSERT_EQ(getsockname(fd, reinterpret_cast<sockaddr *>(&address), &length), 0);
  settings.ptp_address = "localhost";
  settings.general_port = ntohs(address.sin6_port);
  EXPECT_THROW({ nqptp::Runtime runtime(settings); }, std::system_error);
  EXPECT_NO_THROW({ Descriptor released(bind_udp(settings.event_port, "127.0.0.1")); });
  EXPECT_NO_THROW({ Descriptor released(bind_udp(settings.general_port, "127.0.0.1")); });
  expectMemoryRemoved();
}

int fail_truncate(int, off_t) noexcept { errno = ENOSPC; return -1; }
void *fail_map(void *, size_t, int, int, int, off_t) noexcept { errno = ENOMEM; return MAP_FAILED; }

TEST_F(NqptpRuntime, RemovesItsOwnedObjectAndSocketsWhenSizingFails) {
  nqptp::MappingOperations mapping;
  mapping.truncate = fail_truncate;
  EXPECT_THROW({ nqptp::Runtime runtime(settings, mapping); }, std::system_error);
  expectPortsReleased();
  expectMemoryRemoved();
}

TEST_F(NqptpRuntime, RemovesItsOwnedObjectAndSocketsWhenMappingFails) {
  nqptp::MappingOperations mapping;
  mapping.map = fail_map;
  EXPECT_THROW({ nqptp::Runtime runtime(settings, mapping); }, std::system_error);
  expectPortsReleased();
  expectMemoryRemoved();
}

TEST_F(NqptpRuntime, NormalShutdownPermitsRestartWithIdenticalEndpoints) {
  { nqptp::Runtime runtime(settings);
    Descriptor reader(shm_open(settings.shared_memory_name.c_str(), O_RDONLY, 0));
    ASSERT_GE(reader.get(), 0);
    shm_structure data{};
    ASSERT_EQ(pread(reader.get(), &data, sizeof(data), 0), sizeof(data));
    EXPECT_EQ(data.version, NQPTP_SHM_STRUCTURES_VERSION);
    EXPECT_EQ(data.main.master_clock_id, 0);
  }
  expectPortsReleased();
  expectMemoryRemoved();
  EXPECT_NO_THROW({ nqptp::Runtime restarted(settings); });
}

TEST_F(NqptpRuntime, SignalTerminationReleasesResourcesBeforeProcessExit) {
  settings.handle_signals = true;
  int descriptors[2];
  ASSERT_EQ(pipe2(descriptors, O_CLOEXEC), 0);
  Descriptor read_end(descriptors[0]), write_end(descriptors[1]);
  const pid_t child = fork();
  ASSERT_GE(child, 0);
  if (child == 0) {
    int result = 0;
    try {
      nqptp::Runtime runtime(settings);
      const char ready = 'R';
      if (write(write_end.get(), &ready, 1) != 1) _exit(2);
      runtime.run();
    } catch (...) { result = 3; }
    _exit(result);
  }
  pollfd ready{read_end.get(), POLLIN, 0};
  if (poll(&ready, 1, 2000) != 1) {
    kill(child, SIGKILL);
    waitpid(child, nullptr, 0);
    FAIL() << "Companion child did not finish startup";
  }
  ASSERT_EQ(kill(child, SIGTERM), 0);
  int status{};
  ASSERT_EQ(waitpid(child, &status, 0), child);
  EXPECT_TRUE(WIFEXITED(status));
  EXPECT_EQ(WEXITSTATUS(status), 0);
  expectMemoryRemoved();
  expectPortsReleased();
  EXPECT_NO_THROW({ nqptp::Runtime restarted(settings); });
}
}
