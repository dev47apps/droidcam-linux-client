/* DroidCam & DroidCamX (C) 2010-
 * Author: Aram G. (dev47@dev47apps.com)
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * Use at your own risk. See README file for more details.
 */
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <arpa/inet.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/sdp.h>
#include <bluetooth/sdp_lib.h>
#include <bluetooth/rfcomm.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include "common.h"
#include "connection.h"

sdp_record_t  * sdpRecord;
sdp_session_t * sdpSession;
SOCKET btServerSocket = INVALID_SOCKET, wifiServerSocket = INVALID_SOCKET;
extern int v_running;

SOCKET connectDroidCam(char * ip, int port)
{
	struct sockaddr_in sin;
	SOCKET sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
	
	if(sock == INVALID_SOCKET)
	{
		MSG_LASTERROR("Error");
	}
	else 
	{
		sin.sin_family = AF_INET;
		sin.sin_addr.s_addr = inet_addr(ip);
		sin.sin_port = htons(port);

		printf("connect IP='%x' (%s); p:%d\n", sin.sin_addr.s_addr, ip, port);

		if (connect(sock, (struct sockaddr*)&sin, sizeof(sin)) < 0)
		{
			printf("connect failed %d '%s'\n", errno, strerror(errno));
			MSG_ERROR("Connect failed, please try again.\nCheck IP and Port.\nCheck network connection.");
			close(sock);
			sock = INVALID_SOCKET;
		} 
	}
	return sock;
}
void disconnect(SOCKET s){
	close(s);
}

int SendRecv(int doSend, char * buffer, int bytes, SOCKET s)
{
	int retCode;
	char * ptr = buffer;
	
	while (bytes > 0) 
	{
		retCode = (doSend) ? send(s, ptr, bytes, 0) : recv(s, ptr, bytes, 0);
		if (retCode <= 0 ){ // closed or error
			goto _error_out;
		}
		ptr += retCode;
		bytes -= retCode;
	}
	
	retCode = 1;
_error_out:
	return retCode;
}

static int RegisterSdp(uint8_t port)
{
	int ret = 1;

	// {6A841273-4A97-4eed-A299-C23D571A54E7}
    // {00001101-0000-1000-8000-00805F9B34FB}
	uint8_t svc_uuid_int[] = {0x00, 0x00, 0x11, 0x01, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0x80, 0x5f, 0x9b, 0x34, 0xfb};

	uuid_t root_uuid, l2cap_uuid, rfcomm_uuid, svc_uuid;
	sdp_list_t *l2cap_list = 0, 
	*rfcomm_list = 0,
	*root_list = 0,
	*proto_list = 0, 
	*access_proto_list = 0;
	sdp_data_t* channel = 0;

	sdpRecord = sdp_record_alloc();

	// set the general service ID
	sdp_uuid128_create(&svc_uuid, &svc_uuid_int);
	sdp_set_service_id(sdpRecord, svc_uuid);

	// make the service record publicly browsable
	sdp_uuid16_create(&root_uuid, PUBLIC_BROWSE_GROUP);
	root_list = sdp_list_append(0, &root_uuid);
	sdp_set_browse_groups(sdpRecord, root_list);

	// set l2cap information
	sdp_uuid16_create(&l2cap_uuid, L2CAP_UUID);
	l2cap_list = sdp_list_append(0, &l2cap_uuid);
	proto_list = sdp_list_append(0, l2cap_list);

	// set rfcomm information
	sdp_uuid16_create(&rfcomm_uuid, RFCOMM_UUID);
	channel = sdp_data_alloc(SDP_UINT8, &port);
	rfcomm_list = sdp_list_append(0, &rfcomm_uuid);
	sdp_list_append(rfcomm_list, channel);
	sdp_list_append(proto_list, rfcomm_list);

	// attach protocol information to service record
	access_proto_list = sdp_list_append(0, proto_list);
	sdp_set_access_protos(sdpRecord, access_proto_list);

	// set the name, provider, and description
	sdp_set_info_attr(sdpRecord, "DroidCam", "", "Android Webcam");

	// connect to the local SDP server, register the service record
	sdpSession = sdp_connect(BDADDR_ANY, BDADDR_LOCAL, SDP_RETRY_IF_BUSY);

	if(sdp_record_register(sdpSession, sdpRecord, 0))
	{
		MSG_ERROR("Could not register Bluetooth service");
		ret = 0;
	}
	sdp_data_free(channel);
	sdp_list_free(l2cap_list, 0);
	sdp_list_free(rfcomm_list, 0);
	sdp_list_free(root_list, 0);
	sdp_list_free(access_proto_list, 0);
	
	return ret;
}

static int StartBthServer()
{
	int ret = 0;
	int flags = 0;
	uint8_t port = 0;
	struct sockaddr_rc localAddr = { 0 };

	if((btServerSocket = socket(AF_BLUETOOTH, SOCK_STREAM, BTPROTO_RFCOMM)) == INVALID_SOCKET){
		MSG_LASTERROR("Could not create bluetooth socket");
		goto _error_out;
	}

	// bind socket to 1st available port of the first available local bluetooth adapter
	localAddr.rc_family = AF_BLUETOOTH;
	localAddr.rc_bdaddr = *BDADDR_ANY;
	
	for (port = 1; port < 30; port++)
	{
		dbgprint("Trying port %d\n", port);
		localAddr.rc_channel = htons(port);
		if(bind(btServerSocket, (struct sockaddr*) &localAddr, sizeof(localAddr)) == 0)
		{
			printf("bind bth on port %d\n", port);
			break;
		}
	}
	
	if (port == 30){
		MSG_ERROR("Could not bind bluetooth socket");
		goto _error_out;
	}

	if ( !RegisterSdp(port) )
		goto _error_out;

	if(listen(btServerSocket, 1) < 0)
	{
		MSG_LASTERROR("Could not listen on bt socket");
		goto _error_out;
	}

	if((flags = fcntl(btServerSocket, F_GETFL, NULL)) <  0)
	{
		MSG_LASTERROR("Could not get bt socket flags");
		goto _error_out;
	}
	flags |= O_NONBLOCK;
	fcntl(btServerSocket, F_SETFL, flags);
	
	ret = 1;
	goto _exitOk;

_error_out:
	if (btServerSocket != INVALID_SOCKET){
		close(btServerSocket);
		btServerSocket = INVALID_SOCKET;
	}

_exitOk:
	return ret;
}

static int StartInetServer(int port)
{
	int flags = 0;
	int ret = 0;
	struct sockaddr_in sin;

	sin.sin_family	  = AF_INET;
	sin.sin_addr.s_addr = INADDR_ANY;
	sin.sin_port		= htons(port);

	wifiServerSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if(wifiServerSocket == INVALID_SOCKET)
	{
		MSG_LASTERROR("Could not create socket");
		goto _error_out;
	}

	if(bind(wifiServerSocket, (struct sockaddr*)&sin, sizeof(sin)) < 0)
	{
		printf("bind( .. ) Failed (Error %d: %s) \n", errno, strerror(errno));
		goto _error_out;
	}
	if(listen(wifiServerSocket, 1) < 0)
	{
		printf("listen( .. ) Failed (Error %d: %s) \n", errno, strerror(errno));
		goto _error_out;
	}

	flags = fcntl(wifiServerSocket, F_GETFL, NULL);
	if(flags < 0)
	{
		printf("fcntl( .. ) Failed (Error %d: %s) \n", errno, strerror(errno));
		goto _error_out;
	}
	flags |= O_NONBLOCK;
	fcntl(wifiServerSocket, F_SETFL, flags);

	ret = 1;
	goto _exitOk;

_error_out:
	if (wifiServerSocket != INVALID_SOCKET){
		close(wifiServerSocket);
		wifiServerSocket = INVALID_SOCKET;
	}

_exitOk:
	return ret;
}

void connection_cleanup(){
	if (btServerSocket != INVALID_SOCKET){
		close(btServerSocket);
		btServerSocket = INVALID_SOCKET;
	}
	 if(sdpRecord != NULL)
	{
		sdp_record_unregister(sdpSession, sdpRecord);
		sdpRecord = NULL;
	}
	if(sdpSession != NULL)
	{
		sdp_close(sdpSession);
		sdpSession = NULL;
	}
	
	if (wifiServerSocket != INVALID_SOCKET)
		close(wifiServerSocket);
}

SOCKET accept_bth_connection()
{
	int flags;
	SOCKET client =  INVALID_SOCKET;

	dbgprint("serverSocket=%d\n", btServerSocket);
	if (btServerSocket == INVALID_SOCKET && !StartBthServer()) 
		goto _error_out;

	dbgprint("waiting..");
	while(v_running && (client = accept(btServerSocket, NULL, NULL)) == INVALID_SOCKET)
	{
		if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR){
			usleep(50000);
			continue;
		}
		MSG_LASTERROR("Accept Failed");
		break;
	}

	if (client != INVALID_SOCKET) {// Blocking..
		flags = fcntl(btServerSocket, F_GETFL, NULL);
		flags |= O_NONBLOCK;
		fcntl(btServerSocket, F_SETFL, flags);
	}

_error_out:
	return client;
}

SOCKET accept_inet_connection(int port)
{
	int flags;
	SOCKET client =  INVALID_SOCKET;

	dbgprint("serverSocket=%d\n", wifiServerSocket);
	if (wifiServerSocket == INVALID_SOCKET && !StartInetServer(port)) 
		goto _error_out;

	dbgprint("waiting on port %d..", port);
	while(v_running && (client = accept(wifiServerSocket, NULL, NULL)) == INVALID_SOCKET)
	{
		if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR){
			usleep(50000);
			continue;
		}
		MSG_LASTERROR("Accept Failed");
		break;
	}

	if (client != INVALID_SOCKET) {// Blocking..
		flags = fcntl(wifiServerSocket, F_GETFL, NULL);
		flags |= O_NONBLOCK;
		fcntl(wifiServerSocket, F_SETFL, flags);
	}

_error_out:
	return client;
}
