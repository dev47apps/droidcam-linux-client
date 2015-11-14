/* DroidCam & DroidCamX (C) 2010-
 * https://github.com/aramg
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

SOCKET connect_droidcam(char * ip, int port);
void connection_cleanup();
void disconnect(SOCKET s);

SOCKET accept_connection(int port);

int SendRecv(int doSend, char * buffer, int bytes, SOCKET s);

#endif
