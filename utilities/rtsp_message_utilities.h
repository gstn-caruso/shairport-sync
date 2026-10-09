#pragma once

#include "player.h"
#include "rtsp.h"

#ifdef __cplusplus
extern "C" {
#endif

rtsp_message *msg_init(void);

void _debug_log_rtsp_message(rtsp_conn_info *conn, const char *filename, const int linenumber,
                             int level, const char *prompt, rtsp_message *message);

#define debug_log_rtsp_message_conn(conn, level, prompt, message)                                  \
  _debug_log_rtsp_message(conn, __FILE__, __LINE__, level, prompt, message)

#define debug_log_rtsp_message(level, prompt, message)                                             \
  _debug_log_rtsp_message(NULL, __FILE__, __LINE__, level, prompt, message)

void _debug_print_msg_headers(rtsp_conn_info *conn, const char *filename, const int linenumber,
                              int level, rtsp_message *msg);
#define debug_print_msg_headers(level, message)                                                    \
  _debug_print_msg_headers(NULL, __FILE__, __LINE__, level, message)

#define debug_print_msg_headers_conn(level, message)                                               \
  _debug_print_msg_headers(conn, __FILE__, __LINE__, level, message)

#ifdef __cplusplus
}

plist_t plistFromMessageBody(const RtspMessage &message);
void replaceBodyFromAllocation(RtspMessage &message, char *bytes, size_t length);
void replaceBodyWithPlist(RtspMessage &message, plist_t plist);
#endif
