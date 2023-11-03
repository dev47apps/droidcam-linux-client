/* DroidCam & DroidCamX (C) 2010-2021
 * https://github.com/dev47apps
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 */

#ifndef __CONN_H__
#define __CONN_H__

#include <stdbool.h>
#include <sys/types.h>

#define INVALID_SOCKET -1
typedef int SOCKET;
typedef long int SOCKET_PTR;

SOCKET Connect(const char* ip, int port, const char **errormsg);
void connection_cleanup();
void disconnect(SOCKET s);

SOCKET accept_connection(int port);
SOCKET CreateUdpSocket(void);
bool Send(const char * buffer, int bytes, SOCKET s);
ssize_t Recv(const char * buffer, int bytes, SOCKET s);
ssize_t RecvAll(const char * buffer, int bytes, SOCKET s);
ssize_t RecvNonBlock(char * buffer, int bytes, SOCKET s);
ssize_t RecvNonBlockUDP(char * buffer, int bytes, SOCKET s);
ssize_t SendUDPMessage(SOCKET s, const char *message, int length, char *ip, int port);

#endif
