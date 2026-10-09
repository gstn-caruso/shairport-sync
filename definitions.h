#ifndef _DEFINITIONS_H
#define _DEFINITIONS_H
#include <sys/socket.h>
#include "config.h"
#ifndef __linux__
#error "This receiver supports Linux only"
#endif
#define SHAIRPORT_SYNC_DEVICE_NAMESPACE "01d7c137-6316-455d-a52f-dfb529f26adf"
#ifdef AF_INET6
#define SOCKADDR struct sockaddr_storage
#define SAFAMILY ss_family
#else
#define SOCKADDR struct sockaddr
#define SAFAMILY sa_family
#endif
#endif
