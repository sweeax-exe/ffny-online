/* SPDX-License-Identifier: GPL-3.0-or-later
 * BSD-socket stand-ins for the Winsock calls the online code makes, so
 * netplay.c and netplay_servers.c compile as-is on POSIX. Include after
 * the system headers, in the same place winsock2.h was included. */
#if !defined(_WIN32)
#ifndef NETPLAY_SOCKETS_POSIX_H
#define NETPLAY_SOCKETS_POSIX_H

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

typedef int SOCKET;
#define INVALID_SOCKET  (-1)
#define SOCKET_ERROR    (-1)
#define SD_BOTH         SHUT_RDWR

static __inline int closesocket(SOCKET s) { return close(s); }
typedef struct { unsigned short wVersion, wHighVersion; char szDescription[1]; } WSADATA;
static __inline int ioctlsocket(SOCKET s, long cmd, unsigned long *argp)
{
    return cmd == FIONBIO ? fcntl(s, F_SETFL, *argp ? O_NONBLOCK : 0) : -1;
}
#define WSAStartup(major, minor)      (0)      /* no per-process init on POSIX */
#define WSACleanup()                  (0)
#define WSAGetLastError()             (errno)
#define WSAEWOULDBLOCK                EWOULDBLOCK
#define WSAECONNRESET                 ECONNRESET
#define WSAEMSGSIZE                   EMSGSIZE

/* select() on Windows ignores the first argument; POSIX wants max fd + 1. */
static __inline int netplay_select(int sock, fd_set *rd, struct timeval *tv)
{
    return select(sock + 1, rd, NULL, NULL, tv);
}
#define select_(sock, rd, tv) netplay_select(sock, rd, tv)

#endif
#endif
