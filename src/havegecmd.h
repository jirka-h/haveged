/**
 ** Provide HAVEGE socket communication API
 **
 ** Copyright 2018-2020 Jirka Hladky hladky DOT jiri AT gmail DOT com
 ** Copyright 2018 Werner Fink <werner@suse.de>
 **
 ** This program is free software: you can redistribute it and/or modify
 ** it under the terms of the GNU General Public License as published by
 ** the Free Software Foundation, either version 3 of the License, or
 ** (at your option) any later version.
 **
 ** This program is distributed in the hope that it will be useful,
 ** but WITHOUT ANY WARRANTY; without even the implied warranty of
 ** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 ** GNU General Public License for more details.
 **
 ** You should have received a copy of the GNU General Public License
 ** along with this program.  If not, see <http://www.gnu.org/licenses/>.
 **
 */

#ifndef HAVEGECMD_H
#define HAVEGECMD_H

#ifdef __cplusplus
extern "C" {
#endif

#include "havege.h"
#include "haveged.h"

#include <sys/types.h>
#include <sys/socket.h>

#define HAVEGED_SOCKET_PATH      "\0/sys/entropy/haveged"
#define MAGIC_CHROOT             'R'
  
#ifndef SOCK_CLOEXEC
#define SOCK_CLOEXEC 0
#endif

#ifndef SOCK_NONBLOCK
#define SOCK_NONBLOCK 0
#endif

/**
 * Open and listen on a UNIX socket to get command from there
 */
int cmd_listen(struct pparams *);

/**
 * Open and connect on a UNIX socket to send command over this
 */
int cmd_connect(struct pparams *);

/**
 * Handle arguments in command mode
 */
int getcmd(char *);

/**
 * Handle incomming messages from socket
 */
int socket_handler(int, const volatile char *, char *const [], struct pparams *);

/**
 * Receive incomming messages from socket
 */
ssize_t safein(int, void *, size_t);

/**
 * Send outgoing messages to socket
 */
void safeout(int, const void *, size_t);

/**
 * Socket file descriptor used for communication
 */

extern int socket_fd;

#ifdef __cplusplus
}
#endif

#endif
