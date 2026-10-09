/*
 * mDNS registration handler. This file is part of Shairport.
 * Copyright (c) James Laird 2013
 * Modifications, updates and additions (c) Mike Brady 2014--2025
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include "mdns.h"
#include "common.h"
#include "config.h"
#include <memory.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern mdns_backend mdns_avahi;



void mdns_register(const char **txt_records, const char **secondary_txt_records) {
  char *ap1_service_name = static_cast<char *>(alloca(strlen(config.service_name) + 14));
  char *p = ap1_service_name;
  int i;
  for (i = 0; i < 6; i++) {
    snprintf(p, 3, "%02X", config.ap1_prefix[i]);
    p += 2;
  }
  *p++ = '@';
  strcpy(p, config.service_name);

if (mdns_avahi.mdns_register(ap1_service_name, config.service_name, config.port,
                              txt_records, secondary_txt_records) < 0)
  die("Could not establish Avahi advertisement.");
config.mdns = &mdns_avahi;
}

void mdns_update(const char **txt_records, const char **secondary_txt_records) {
  if ((config.mdns) && (config.mdns->mdns_update)) {
    config.mdns->mdns_update(txt_records, secondary_txt_records);
  } else
    debug(1, "Can't mdns_update -- no mdns_update registered.");
}

void mdns_unregister(void) {
  if (config.mdns) {
    config.mdns->mdns_unregister();
  }
}

void mdns_ls_backends(void) {
  printf("Service discovery: Avahi\n");
}
