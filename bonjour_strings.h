#ifndef _BONJOUR_STRINGS_H
#define _BONJOUR_STRINGS_H

#include "player.h"

extern char *txt_records[128];
extern char *secondary_txt_records[128];

void build_bonjour_strings(rtsp_conn_info *conn);

#endif // _BONJOUR_STRINGS_H
