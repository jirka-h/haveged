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

#include "config.h"

#include <stddef.h>
#include <stdint.h>

#ifndef NO_COMMAND_MODE
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#ifndef HAVE_STRUCT_UCRED
struct ucred
{
   pid_t pid;                      /* PID of sending process.  */
   uid_t uid;                      /* UID of sending process.  */
   gid_t gid;                      /* GID of sending process.  */
};
#endif

#include "havegecmd.h"

int socket_fd;

static void new_root(              /* RETURN: nothing                       */
   const char *root,               /* IN: path of the new root file system  */
   const volatile char *path,      /* IN: path of the haveged executable    */
   char *const argv[],             /* IN: arguments for the haveged process */
   struct pparams *params)         /* IN: input params                      */
{
   int ret;

   fprintf(stderr, "%s: restart in new root: %s\n", params->daemon, root);
   ret = chdir(root);
   if (ret < 0) {
      if (errno != ENOENT)
         error_exit("can't change to working directory : %s", root);
      else
         fprintf(stderr, "%s: can't change to working directory : %s\n", params->daemon, root);
      }
   ret = chroot(".");
   if (ret < 0) {
      if (errno != ENOENT)
         error_exit("can't change root directory");
      else
         fprintf(stderr, "%s: can't change root directory\n", params->daemon);
      }
   ret = chdir("/");
   if (ret < 0) {
      if (errno != ENOENT)
         error_exit("can't change to working directory /");
      else
         fprintf(stderr, "%s: can't change to working directory /\n", params->daemon);
      }
   ret = execv((const char *)path, argv);
   if (ret < 0) {
      if (errno != ENOENT)
         error_exit("can't restart %s", path);
      else
         fprintf(stderr, "%s: can't restart %s\n", params->daemon, path);
      }
}

/**
 * Open and listen on a UNIX socket to get command from there
 */
int cmd_listen(                    /* RETURN: UNIX socket file descriptor */
   struct pparams *params)         /* IN: input params                    */
{
   struct sockaddr_un su = {       /* The abstract UNIX socket of haveged */
      .sun_family = AF_UNIX,
      .sun_path = HAVEGED_SOCKET_PATH,
   };
   const int one = 1;
   int fd, ret;

   fd = socket(PF_UNIX, SOCK_STREAM|SOCK_CLOEXEC|SOCK_NONBLOCK, 0);
   if (fd < 0) {
      fprintf(stderr, "%s: can not open UNIX socket\n", params->daemon);
      goto err;
      }

   ret = setsockopt(fd, SOL_SOCKET, SO_PASSCRED, &one, (socklen_t)sizeof(one));
   if (ret < 0) {
      close(fd);
      fd = -1;
      fprintf(stderr, "%s: can not set option for UNIX socket\n", params->daemon);
      goto err;
      }

   ret = bind(fd, (struct sockaddr *)&su, offsetof(struct sockaddr_un, sun_path) + 1 + strlen(su.sun_path+1));
   if (ret < 0) {
      close(fd);
      fd = -1;
      if (errno != EADDRINUSE)
         fprintf(stderr, "%s: can not bind a name to UNIX socket\n", params->daemon);
      else
         fd = -2;
      goto err;
      }

   ret = listen(fd, SOMAXCONN);
   if (ret < 0) {
      close(fd);
      fd = -1;
      fprintf(stderr, "%s: can not listen on UNIX socket\n", params->daemon);
      goto err;
      }
err:
   return fd;
}

/**
 * Open and connect on a UNIX socket to send command over this
 */
int cmd_connect(                   /* RETURN: UNIX socket file descriptor */
   struct pparams *params)         /* IN: input params                    */
{
   struct sockaddr_un su = {       /* The abstract UNIX socket of haveged */
      .sun_family = AF_UNIX,
      .sun_path = HAVEGED_SOCKET_PATH,
      };
   const int one = 1;
   int fd, ret;

   fd = socket(PF_UNIX, SOCK_STREAM|SOCK_CLOEXEC|SOCK_NONBLOCK, 0);
   if (fd < 0) {
      fprintf(stderr, "%s: can not open UNIX socket\n", params->daemon);
      goto err;
      }

   ret = setsockopt(fd, SOL_SOCKET, SO_PASSCRED, &one, (socklen_t)sizeof(one));
   if (ret < 0) {
      fprintf(stderr, "%s: can not set option for UNIX socket\n", params->daemon);
      close(fd);
      fd = -1;
      goto err;
      }

   ret = connect(fd, (struct sockaddr *)&su, offsetof(struct sockaddr_un, sun_path) + 1 + strlen(su.sun_path+1));
   if (ret < 0) {
      if (errno != ECONNREFUSED)
         fprintf(stderr, "%s: can not connect on UNIX socket\n", params->daemon);
      close(fd);
      fd = -1;
      goto err;
      }
err:
   return fd;
}

/**
 * Handle arguments in command mode
 */
int getcmd(                        /* RETURN: success or error      */
   char *arg)                      /* handle current known commands */
{
   static const struct {
      const char* cmd;
      const int req;
      const int arg;
      const char* opt;
   } cmds[] = {
      { "root=",      MAGIC_CHROOT, 1, NULL },      /* New root */
      {}
   }, *cmd = cmds;
   int ret = -1;

   if (!arg || !*arg)
      goto err;

   optarg = NULL;
   for (; cmd->cmd; cmd++)
      if (cmd->arg) {
         if (strncmp(cmd->cmd, arg, strlen(cmd->cmd)) == 0) {
            optarg = strchr(arg, '=');
            optarg++;
            ret = cmd->req;
            break;
            }
         }
      else {
         if (strcmp(cmd->cmd, arg) == 0) {
            ret = cmd->req;
            break;
            }
         }
err:
   return ret;
}

/**
 * Handle incomming messages from socket
 */
int socket_handler(                /* RETURN: closed file descriptor        */
   int fd,                         /* IN: connected socket file descriptor  */
   const volatile char *path,      /* IN: path of the haveged executable    */
   char *const argv[],             /* IN: arguments for the haveged process */
   struct pparams *params)         /* IN: input params                      */
{
   struct ucred cred = {};
   unsigned char magic[2], *ptr;
   char *enqry;
   char *optarg = NULL;
   socklen_t clen;
   int ret = -1, len;

   if (fd < 0) {
      fprintf(stderr, "%s: no connection jet\n", params->daemon);
      }

   ptr = &magic[0];
   len = sizeof(magic);
   ret = safein(fd, ptr, len);

   if (magic[1] == '\002') {       /* read argument provided */
      unsigned char alen;

      ret = safein(fd, &alen, sizeof(unsigned char));

      optarg = calloc(alen, sizeof(char));
      if (!optarg)
          error_exit("can not allocate memory for message from UNIX socket");

      ptr = (unsigned char*)optarg;
      len = alen;
      ret = safein(fd, ptr, len);
      }

   clen = sizeof(struct ucred);
   ret = getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cred, &clen);
   if (ret < 0) {
      fprintf(stderr, "%s: can not get credentials from UNIX socket part1\n", params->daemon);
      goto out;
      }
   if (clen != sizeof(struct ucred)) {
      fprintf(stderr, "%s: can not get credentials from UNIX socket part2\n", params->daemon);
      goto out;
      }
   if (cred.uid != 0) {
      enqry = "\x15";

      ptr = (unsigned char *)enqry;
      len = (int)strlen(enqry)+1;
      safeout(fd, ptr, len);
      }

   switch (magic[0]) {
      case MAGIC_CHROOT:
         enqry = "\x6";

         ptr = (unsigned char *)enqry;
         len = (int)strlen(enqry)+1;
         safeout(fd, ptr, len);

         new_root(optarg, path, argv, params);
         break;
      default:
         enqry = "\x15";

         ptr = (unsigned char *)enqry;
         len = (int)strlen(enqry)+1;
         safeout(fd, ptr, len);
         break;
      }
out:
   if (optarg)
      free(optarg);
   if (fd > 0) {
      close(fd);
      fd = -1;
      }
   return fd;
}

/**
 * Receive incomming messages from socket
 */
ssize_t safein(                    /* RETURN: read bytes                    */
   int fd,                         /* IN: file descriptor                   */
   void *ptr,                      /* OUT: pointer to buffer                */
   size_t sz)                      /* IN: size of buffer                    */
{
   int saveerr = errno, t;
   ssize_t ret = 0;
   size_t len;

   if (sz > SSIZE_MAX)
      sz = SSIZE_MAX;

   t = 0;
   if ((ioctl(fd, FIONREAD, &t) < 0) || (t <= 0))
      goto out;

   len = (size_t)t;
   if (len > sz)
      len = sz;

   do {
      ssize_t p = recv(fd, ptr, len, MSG_DONTWAIT);
      if (p < 0) {
         if (errno == EINTR)
            continue;
         if (errno == EAGAIN || errno == EWOULDBLOCK)
            break;
         error_exit("Unable to read from socket: %d", socket_fd);
         }
      ptr += p;
      ret += p;
      len -= p;
      }
   while (len > 0);
out:
   errno = saveerr;
   return ret;
}

/**
 * Send outgoing messages to socket
 */
void safeout(                      /* RETURN: nothing                       */
   int fd,                         /* IN: file descriptor                   */
   const void *ptr,                /* IN: pointer to buffer                 */
   size_t len)                     /* IN: size of buffer                    */
{
   int saveerr = errno;

   do {
      ssize_t p = send(fd, ptr, len, MSG_NOSIGNAL);
      if (p < 0) {
         if (errno == EINTR)
                     continue;
         if (errno == EPIPE || errno == EAGAIN || errno == EWOULDBLOCK)
                     break;
         error_exit("Unable to write to socket: %d", fd);
         }
       ptr += p;
       len -= p;
       }
   while (len > 0);

   errno = saveerr;
}

#endif
