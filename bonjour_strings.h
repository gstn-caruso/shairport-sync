#ifndef _BONJOUR_STRINGS_H
#define _BONJOUR_STRINGS_H

#include "player.h"

#ifdef __cplusplus
extern "C" {
#endif

extern char *txt_records[128];
extern char *secondary_txt_records[128];

void build_bonjour_strings(rtsp_conn_info *conn);

#ifdef __cplusplus
}
#endif

#endif // _BONJOUR_STRINGS_H
