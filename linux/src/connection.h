/* DroidCam & DroidCamX (C) 2010-
 * Author: Aram G. (dev47@dev47apps.com)
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * Use at your own risk. See README file for more details.
 */
#ifndef __CONN_H__
#define __CONN_H__

#define INVALID_SOCKET -1
typedef int SOCKET;

SOCKET connectDroidCam(char * ip, int port);
void disconnect(SOCKET s);
void connection_cleanup();

SOCKET accept_bth_connection();
SOCKET accept_inet_connection(int port);

int SendRecv(int doSend, char * buffer, int bytes, SOCKET s);

#endif
