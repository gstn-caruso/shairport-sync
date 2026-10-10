/*
 * This file is part of the nqptp distribution (https://github.com/mikebrady/nqptp).
 * Copyright (c) 2021-2022 Mike Brady.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 2.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 * Commercial licensing is also available.
 */
#include "nqptp.h"
#include "debug.h"
#include "general-utilities.h"
#include "nqptp-clock-sources.h"
#include "nqptp-ptp-definitions.h"
#include "nqptp-utilities.h"
#include <arpa/inet.h>
#include <cstring>
#include <cstdlib>
#include <netdb.h>
#include <string>
#include <unistd.h>
namespace nqptp {
extern sockets_open_bundle *active_sockets;
extern uint16_t active_general_port;
void send_awakening_announcement_sequence(const uint64_t clock_id, const char *clock_ip,
                                          const int ip_family, const uint8_t priority1,
                                          const uint8_t priority2) {
  if (!active_sockets) return;
  const auto &sockets_open_stuff = *active_sockets;
  struct ptp_announce_message *msg;
  size_t msg_length = sizeof(struct ptp_announce_message);
  msg = static_cast<ptp_announce_message *>(malloc(msg_length));
  memset((void *)msg, 0, msg_length);

  uint64_t my_clock_id = get_self_clock_id();
  msg->header.transportSpecificAndMessageID = 0x10 + Announce;
  msg->header.reservedAndVersionPTP = 0x02;
  msg->header.messageLength = htons(sizeof(struct ptp_announce_message));
  msg->header.flags = htons(0x0408);
  hcton64(my_clock_id, &msg->header.clockIdentity[0]);
  msg->header.sourcePortID = htons(32776);
  msg->header.controlField = 0x05;
  msg->header.logMessagePeriod = 0xFE;
  msg->announce.currentUtcOffset = htons(37);
  hcton64(my_clock_id, &msg->announce.grandmasterIdentity[0]);
  uint32_t my_clock_quality = 0xf8fe436a;
  msg->announce.grandmasterClockQuality = htonl(my_clock_quality);
  if (priority1 > 2) {
    msg->announce.grandmasterPriority1 =
        priority1 - 1; // make this announcement seem better than the clock we are about to ping
    msg->announce.grandmasterPriority2 = priority2;
  } else {
    warn("Cannot select a suitable priority for pinging clock %" PRIx64 " at %s.", clock_id,
         clock_ip);
    msg->announce.grandmasterPriority1 = 248;
    msg->announce.grandmasterPriority2 = 248;
  }
  msg->announce.timeSource = 160; // Internal Oscillator

  // get the socket for the correct port -- 320 -- and family -- IPv4 or IPv6 -- to send it
  // from.

  int s = 0;
  unsigned t;
  for (t = 0; t < sockets_open_stuff.sockets_open; t++) {
    if ((sockets_open_stuff.sockets[t].port == active_general_port) &&
        (sockets_open_stuff.sockets[t].family == ip_family))
      s = sockets_open_stuff.sockets[t].number;
  }
  if (s == 0) {
    debug(1, "sending socket not found for clock %" PRIx64 " at %s, family %s.", clock_id, clock_ip,
          ip_family == AF_INET    ? "IPv4"
          : ip_family == AF_INET6 ? "IPv6"
                                  : "Unknown");
  } else {
    // debug(1, "Send message from socket %d.", s);

    const std::string portname = std::to_string(active_general_port);
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = 0;
    hints.ai_flags = AI_ADDRCONFIG;
    struct addrinfo *res = NULL;
    int err = getaddrinfo(clock_ip, portname.c_str(), &hints, &res);
    if (err != 0) {
      debug(1, "failed to resolve remote socket address (err=%d)", err);
    } else {
      // here, we have the destination, so send it

      // debug_print_buffer(1, (char *)msg, msg_length);
      int ret = sendto(s, msg, msg_length, 0, res->ai_addr, res->ai_addrlen);
      if (ret == -1)
        debug(1, "result of sendto is %d.", ret);
      debug(2, "Send awaken Announce message to clock \"%" PRIx64 "\" at %s on %s.", clock_id,
            clock_ip, ip_family == AF_INET6 ? "IPv6" : "IPv4");

      if (priority1 < 254) {
        msg->announce.grandmasterPriority1 =
            priority1 + 1; // make this announcement seem worse than the clock we about to ping
      } else {
        warn("Cannot select a suitable priority for second ping of clock %" PRIx64 " at %s.",
             clock_id, clock_ip);
        msg->announce.grandmasterPriority1 = 250;
      }

      msg->announce.grandmasterPriority2 = priority2;
      usleep(150000);
      ret = sendto(s, msg, msg_length, 0, res->ai_addr, res->ai_addrlen);
      if (ret == -1)
        debug(1, "result of second sendto is %d.", ret);
      freeaddrinfo(res);
    }
  }
  free(msg);
}

uint64_t broadcasting_task(uint64_t call_time, [[maybe_unused]] void *private_data) {
  clock_source_private_data *clocks_private = (clock_source_private_data *)private_data;
  int i;
  for (i = 0; i < MAX_CLOCKS; i++) {

    /*
        int is_a_master = 0;
        int temp_client_id;

        for (temp_client_id = 0; temp_client_id < MAX_CLIENTS; temp_client_id++)
          if ((clocks_private->client_flags[temp_client_id] & (1 << clock_is_master)) != 0)
            is_a_master = 1;
        // only process it if it's a master somewhere...
        if ((is_a_master != 0) && (clocks_private[i].announcements_without_followups == 3)) {
    */
    if (clocks_private[i].announcements_without_followups == 3) {
      if (clocks_private[i].follow_up_number == 0) {
        debug(1,
              "Attempt to awaken a silent clock %" PRIx64
              ", index %u, at follow_up_number %u at IP %s.",
              clocks_private[i].clock_id, i, clocks_private[i].follow_up_number,
              clocks_private[i].ip);

        // send an Announce message to attempt to waken this silent PTP clock by
        // getting it to negotiate with an apparently better clock
        // that then immediately sends another Announce message indicating that it's inferior

        clocks_private[i].announcements_without_followups++; // set to 4 to indicate done/parked
        send_awakening_announcement_sequence(
            clocks_private[i].clock_id, clocks_private[i].ip, clocks_private[i].family,
            clocks_private[i].grandmasterPriority1, clocks_private[i].grandmasterPriority2);
      } else {
        debug(1,
              "Silent clock %" PRIx64
              " detected, index %u, at follow_up_number %u at IP %s. No attempt to awaken it.",
              clocks_private[i].clock_id, i, clocks_private[i].follow_up_number,
              clocks_private[i].ip);
      }
    }
  }

  /*
    uint64_t announce_interval = 1;
    announce_interval = announce_interval << (8 + aPTPinitialLogAnnounceInterval);
    announce_interval = announce_interval * 1000000000;
    announce_interval = announce_interval >> 8; // nanoseconds
    return call_time + announce_interval;
  */
  return call_time + 50000000;
}

}
