#include "runtime.hpp"
#include "general-utilities.h"
#include "nqptp-ptp-definitions.h"
#include "runtime/common.h"
#include "timing/ptp-utilities.h"
#include "timing/clock_status.h"
#include <gtest/gtest.h>
#include <arpa/inet.h>
#include <array>
#include <cstring>
#include <memory>
#include <span>
#include <system_error>

namespace {
class DatagramSender {
public:
  explicit DatagramSender(uint16_t source_port = 0) {
    fd_ = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd_ < 0) throw std::system_error(errno, std::generic_category());
    sockaddr_in source{};
    source.sin_family = AF_INET;
    source.sin_port = htons(source_port);
    inet_pton(AF_INET, "127.0.0.3", &source.sin_addr);
    if (bind(fd_, reinterpret_cast<sockaddr *>(&source), sizeof(source)) < 0) {
      const int error = errno;
      close(fd_);
      throw std::system_error(error, std::generic_category());
    }
  }
  ~DatagramSender() { close(fd_); }
  DatagramSender(const DatagramSender &) = delete;
  void send(std::span<const char> bytes, uint16_t port) {
    sockaddr_in destination{};
    destination.sin_family = AF_INET;
    destination.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.2", &destination.sin_addr);
    ASSERT_EQ(sendto(fd_, bytes.data(), bytes.size(), 0,
                      reinterpret_cast<sockaddr *>(&destination), sizeof(destination)),
              static_cast<ssize_t>(bytes.size()));
  }
private:
  int fd_ = -1;
};

class NqptpIntegration : public ::testing::Test {
protected:
  nqptp::Endpoints settings;
  std::unique_ptr<nqptp::Runtime> runtime;
  std::unique_ptr<DatagramSender> event_sender, general_sender, control_sender;
  char *previous_name = nullptr;
  void SetUp() override {
    settings.ptp_address = "127.0.0.2";
    settings.control_address = "127.0.0.2";
    settings.event_port = settings.general_port = settings.control_port = 0;
    settings.shared_memory_name = "/shairport-nqptp-integration-" + std::to_string(getpid());
    settings.handle_signals = false;
    runtime = std::make_unique<nqptp::Runtime>(settings);
    settings = runtime->endpoints();
    event_sender = std::make_unique<DatagramSender>(settings.event_port);
    general_sender = std::make_unique<DatagramSender>(settings.general_port);
    control_sender = std::make_unique<DatagramSender>();
    previous_name = config.nqptp_shared_memory_interface_name;
    config.nqptp_shared_memory_interface_name = settings.shared_memory_name.data();
    ASSERT_EQ(ptp_shm_interface_open(), 0);
    ASSERT_EQ(ptp_get_clock_version(), NQPTP_SHM_STRUCTURES_VERSION);
  }
  void TearDown() override {
    ptp_shm_interface_close();
    config.nqptp_shared_memory_interface_name = previous_name;
    runtime.reset();
  }
  void control(const std::string &command) {
    std::string message = settings.shared_memory_name + " " + command;
    message.push_back('\0');
    control_sender->send(message, settings.control_port);
    ASSERT_TRUE(runtime->poll_once(50));
  }
  template <class Packet> void header(Packet &packet, uint8_t kind) {
    packet.header.transportSpecificAndMessageID = 0x10 | kind;
    packet.header.reservedAndVersionPTP = 2;
    packet.header.messageLength = htons(sizeof(packet));
    nqptp::hcton64(42, packet.header.clockIdentity);
  }
  template <class Packet> void send(Packet &packet, DatagramSender &sender, uint16_t port) {
    sender.send({reinterpret_cast<const char *>(&packet), sizeof(packet)}, port);
    ASSERT_TRUE(runtime->poll_once(50));
  }
  void announce() {
    nqptp::ptp_announce_message packet{};
    header(packet, nqptp::Announce);
    nqptp::hcton64(42, packet.announce.grandmasterIdentity);
    packet.announce.grandmasterPriority1 = 248;
    send(packet, *general_sender, settings.general_port);
  }
  nqptp::ptp_follow_up_message followPacket(uint64_t origin) {
    nqptp::ptp_follow_up_message packet{};
    header(packet, nqptp::Follow_Up);
    const uint64_t seconds = origin / 1000000000;
    const uint16_t seconds_high = htons(seconds >> 32);
    const uint32_t seconds_low = htonl(seconds);
    const uint32_t nanos = htonl(origin % 1000000000);
    std::memcpy(packet.follow_up.preciseOriginTimestamp, &seconds_high, sizeof(seconds_high));
    std::memcpy(packet.follow_up.preciseOriginTimestamp + 2, &seconds_low, sizeof(seconds_low));
    std::memcpy(packet.follow_up.preciseOriginTimestamp + 6, &nanos, sizeof(nanos));
    return packet;
  }
  void start() { control("T 127.0.0.3"); control("B"); announce(); }
  int sample(uint64_t &id, uint64_t &time, uint64_t &offset, uint64_t &start) {
    return ptp_get_clock_info(&id, &time, &offset, &start);
  }
};

TEST_F(NqptpIntegration, ReceiverReadsClockFromRealCompanionSocketsAndSharedMemory) {
  uint64_t id{}, time{}, offset{}, master_start{};
  ASSERT_EQ(sample(id, time, offset, master_start), clock_no_master);
  start();
  nqptp::ptp_sync_message sync{};
  header(sync, nqptp::Sync);
  send(sync, *event_sender, settings.event_port);
  const uint64_t origin = nqptp::get_time_now() + 9000000000;
  auto follow = followPacket(origin);
  send(follow, *general_sender, settings.general_port);
  ASSERT_EQ(sample(id, time, offset, master_start), clock_ok);
  EXPECT_EQ(id, 42);
  EXPECT_EQ(time + offset, origin);
  EXPECT_EQ(master_start, time);
  EXPECT_GT(time, 0);
  control("P");
  ASSERT_EQ(sample(id, time, offset, master_start), clock_ok);
  control("E");
  follow = followPacket(nqptp::get_time_now() + 9000000000);
  send(follow, *general_sender, settings.general_port);
  EXPECT_EQ(sample(id, time, offset, master_start), clock_no_master);
  control("B");
  follow = followPacket(nqptp::get_time_now() + 9000000000);
  send(follow, *general_sender, settings.general_port);
  EXPECT_EQ(sample(id, time, offset, master_start), clock_ok);
  control("T");
  EXPECT_EQ(sample(id, time, offset, master_start), clock_no_master);
}

TEST_F(NqptpIntegration, RejectsDatagramsWithWrongSourcePortVersionLengthOrTruncation) {
  start();
  uint64_t id{}, time{}, offset{}, master_start{};
  auto follow = followPacket(nqptp::get_time_now() + 9000000000);
  send(follow, *control_sender, settings.general_port);
  EXPECT_EQ(sample(id, time, offset, master_start), clock_no_master);
  follow.header.reservedAndVersionPTP = 1;
  send(follow, *general_sender, settings.general_port);
  EXPECT_EQ(sample(id, time, offset, master_start), clock_no_master);
  follow.header.reservedAndVersionPTP = 2;
  follow.header.messageLength = htons(sizeof(follow) - 1);
  send(follow, *general_sender, settings.general_port);
  EXPECT_EQ(sample(id, time, offset, master_start), clock_no_master);
  std::array<char, 5000> oversized{};
  std::memcpy(oversized.data(), &follow, sizeof(follow));
  general_sender->send(oversized, settings.general_port);
  ASSERT_TRUE(runtime->poll_once(50));
  EXPECT_EQ(sample(id, time, offset, master_start), clock_no_master);
  control("B extra");
  follow.header.messageLength = htons(sizeof(follow));
  send(follow, *general_sender, settings.general_port);
  EXPECT_EQ(sample(id, time, offset, master_start), clock_ok);
  EXPECT_EQ(id, 42);
}

TEST_F(NqptpIntegration, ReceiverRemapsAfterCompanionRestart) {
  start();
  auto follow = followPacket(nqptp::get_time_now() + 9000000000);
  send(follow, *general_sender, settings.general_port);
  uint64_t id{}, time{}, offset{}, master_start{};
  ASSERT_EQ(sample(id, time, offset, master_start), clock_ok);
  runtime.reset();
  runtime = std::make_unique<nqptp::Runtime>(settings);
  EXPECT_EQ(sample(id, time, offset, master_start), clock_ok);
  ASSERT_EQ(ptp_shm_interface_close(), 0);
  ASSERT_EQ(ptp_shm_interface_open(), 0);
  EXPECT_EQ(sample(id, time, offset, master_start), clock_no_master);
  start();
  follow = followPacket(nqptp::get_time_now() + 9000000000);
  send(follow, *general_sender, settings.general_port);
  EXPECT_EQ(sample(id, time, offset, master_start), clock_ok);
  EXPECT_EQ(id, 42);
}
}
