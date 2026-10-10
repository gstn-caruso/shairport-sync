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

#include "nqptp-clock-sources.h"
#include "debug.h"
#include "general-utilities.h"
#include "nqptp-ptp-definitions.h"
#include <arpa/inet.h>
#include <errno.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h> // for ftruncate and others
#include <unistd.h>    // for ftruncate and others

#include <fcntl.h>      /* For O_* constants */
#include <sys/mman.h>   // for shared memory stuff
#include <sys/select.h> // for fd_set
#include <sys/stat.h>   // umask

#ifdef CONFIG_FOR_FREEBSD
#include <netinet/in.h>
#endif

#ifndef FIELD_SIZEOF
#define FIELD_SIZEOF(t, f) (sizeof(((t *)0)->f))
#endif

namespace nqptp {


struct shm_structure *shared_memory;

clock_source_private_data clocks_private[MAX_CLOCKS];
int find_clock_source_record(char *sender_string, clock_source_private_data *clocks_private_info) {
  // return the index of the clock in the clock information arrays or -1
  int response = -1;
  int i = 0;
  int found = 0;
  while ((found == 0) && (i < MAX_CLOCKS)) {
    if (((clocks_private_info[i].flags & (1 << clock_is_in_use)) != 0) &&
        (strcasecmp(sender_string, (const char *)&clocks_private_info[i].ip) == 0))
      found = 1;
    else
      i++;
  }
  if (found == 1)
    response = i;
  return response;
}

int create_clock_source_record(char *sender_string,
                               clock_source_private_data *clocks_private_info) {
  // return the index of a clock entry in the clock information arrays or -1 if full
  // initialise the entries in the shared and private arrays
  int response = -1;
  int i = 0;
  int found = 0; // trying to find an unused entry
  while ((found == 0) && (i < MAX_CLOCKS)) {
    if ((clocks_private_info[i].flags & (1 << clock_is_in_use)) == 0)
      found = 1;
    else
      i++;
  }

  if (found == 1) {
    int family = 0;

    // check its ipv4/6 family -- derived from https://stackoverflow.com/a/3736377, with thanks.
    struct addrinfo hint, *res = NULL;
    memset(&hint, '\0', sizeof hint);
    hint.ai_family = PF_UNSPEC;
    hint.ai_flags = AI_NUMERICHOST;
    if (getaddrinfo(sender_string, NULL, &hint, &res) == 0) {
      family = res->ai_family;
      freeaddrinfo(res);
      response = i;
      memset(&clocks_private_info[i], 0, sizeof(clock_source_private_data));
      strncpy((char *)&clocks_private_info[i].ip, sender_string,
              FIELD_SIZEOF(clock_source_private_data, ip) - 1);
      clocks_private_info[i].family = family;
      clocks_private_info[i].flags |= (1 << clock_is_in_use);
      debug(2, "create record for ip: %s, family: %s.", &clocks_private_info[i].ip,
            clocks_private_info[i].family == AF_INET6 ? "IPv6" : "IPv4");
    } else {
      debug(1, "cannot getaddrinfo for ip: %s.", &clocks_private_info[i].ip);
    }
  } else {
    debug(1, "Clock tables full!");
  }
  return response;
}

void update_master_clock_info(uint64_t master_clock_id, const char *ip, uint64_t local_time,
                              uint64_t local_to_master_offset, uint64_t mastership_start_time) {
  // to ensure that a full update has taken place, the
  // reader must ensure that the main and secondary
  // structures are identical

  shared_memory->main.master_clock_id = master_clock_id;
  if (ip != NULL) {
    shared_memory->main.master_clock_start_time = mastership_start_time;
    shared_memory->main.local_time = local_time;
    shared_memory->main.local_to_master_time_offset = local_to_master_offset;
  } else {
    shared_memory->main.master_clock_start_time = 0;
    shared_memory->main.local_time = 0;
    shared_memory->main.local_to_master_time_offset = 0;
  }
  __sync_synchronize();
  shared_memory->secondary = shared_memory->main;
  __sync_synchronize();
}

}
