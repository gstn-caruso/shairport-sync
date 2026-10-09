/*
 * RTSP Message Utilities. This file is part of Shairport Sync.
 * Copyright (c) Mike Brady 2026
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

#include "rtsp_message_utilities.h"
#include "../common.h"
#include "../rtsp_message.hpp"
#include <cstdlib>
#include <memory>
#include <new>

#ifdef HAVE_LIBPLIST_GE_2_3_0
#define plist_from_memory(plist_data, length, plist) \
  plist_from_memory((plist_data), (length), (plist), NULL)
#endif

rtsp_message *msg_init(void) {
  auto *message = new (std::nothrow) RtspMessage;
  if (!message)
    die("Unable to allocate an RTSP message");
  return message;
}

void msg_free(rtsp_message **message) {
  delete *message;
  *message = nullptr;
}

void replaceBodyFromAllocation(RtspMessage &message, char *bytes, size_t length) {
  std::unique_ptr<char, decltype(&std::free)> allocation(bytes, &std::free);
  message.replaceBody(bytes ? std::string_view(bytes, length) : std::string_view{});
}

void replaceBodyWithPlist(RtspMessage &message, plist_t plist) {
  char *bytes = nullptr;
  uint32_t length = 0;
  plist_to_bin(plist, &bytes, &length);
  replaceBodyFromAllocation(message, bytes, length);
}

plist_t plistFromMessageBody(const RtspMessage &message) {
  plist_t plist = nullptr;
  if (message.bodyText().starts_with("bplist00"))
    plist_from_memory(message.bodyData(), message.bodyLength(), &plist);
  return plist;
}

char *plist_as_xml_text(plist_t plist) {
  uint32_t length = 0;
  char *bytes = nullptr;
  plist_to_xml(plist, &bytes, &length);
  std::unique_ptr<char, decltype(&std::free)> allocation(bytes, &std::free);
  auto *text = static_cast<char *>(malloc(length + 1));
  if (text) {
    if (length)
      memcpy(text, bytes, length);
    text[length] = '\0';
  }
  return text;
}

void _debug_print_msg_headers(rtsp_conn_info *conn, const char *filename, const int linenumber,
                              int level, rtsp_message *message) {
  if (message->responseCode())
    _debug(filename, linenumber, level, "Response Code: %d.", message->responseCode());
  for (const auto &header : message->headers()) {
    if (conn)
      _debug(filename, linenumber, level, "Connection: %d, Type: \"%s\", content: \"%s\"",
             conn->connection_number, header.name.c_str(), header.value.c_str());
    else
      _debug(filename, linenumber, level, "Type: \"%s\", content: \"%s\"",
             header.name.c_str(), header.value.c_str());
  }
}

void _debug_log_rtsp_message(rtsp_conn_info *conn, const char *filename, const int linenumber,
                            int level, const char *prompt, rtsp_message *message) {
  if (level > debug_level())
    return;
  if (prompt && *prompt)
    _debug(filename, linenumber, level, "%s", prompt);
  _debug_print_msg_headers(conn, filename, linenumber, level, message);
  auto plist = plistFromMessageBody(*message);
  if (plist) {
    auto *text = plist_as_xml_text(plist);
    plist_free(plist);
    if (text) {
      _debug(filename, linenumber, level, "Content length: %u. Content Plist (as XML):\n--\n%s--",
             message->bodyLength(), text);
      free(text);
    }
  } else {
    _debug(filename, linenumber, level, "Content length: %u.", message->bodyLength());
    if (message->bodyLength())
      _debug_print_buffer(filename, linenumber, level,
                          const_cast<char *>(message->bodyData()), message->bodyLength());
  }
}
