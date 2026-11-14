/* SPDX-License-Identifier: GPL-3.0-or-later
 * POSIX stand-ins for the Windows-only pieces of account.c: an HTTPS client
 * (OpenSSL) in place of WinHttp, SHA-256 and random bytes in place of BCrypt,
 * and a protected session file in place of DPAPI (a random key kept beside the
 * data folder, the file itself chmod 600). Include from account.c only. */
#if !defined(_WIN32)
#ifndef ACCOUNT_POSIX_H
#define ACCOUNT_POSIX_H

#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/ssl.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netdb.h>
#include <netinet/in.h>
#include <openssl/sha.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#ifndef MAX_PATH
#define MAX_PATH 1024
#endif
#define SecureZeroMemory(p, n) explicit_bzero((p), (n))
#define LocalFree(p) free(p)
#define _wcsicmp wcscasecmp

/* swprintf's %hs (a narrow string in a wide printf) is a Windows extension. */
#if defined(__GNUC__)
#define swprintf_hs_compat
#endif

#endif
#endif
