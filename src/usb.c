/* DroidCam & DroidCamX (C) 2010-2021
 * https://github.com/dev47apps
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 */

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "usbmuxd.h"

#if __FreeBSD__
#include <sys/wait.h>
#endif

#include "common.h"
#include "settings.h"

void AdbErrorPrint(int rc) {
	switch (rc) {
		case ERROR_ADDING_FORWARD:
			MSG_ERROR("Error adding adb forward: re-attach the device.\n");
			break;
		case ERROR_DEVICE_OFFLINE:
			MSG_ERROR("Device is offline: re-attach the device.");
			break;
		case ERROR_DEVICE_NOTAUTH:
			MSG_ERROR("Device is in unauthorized state:\n"
				"Re-attach device and make sure to Allow the USB debugging connection when prompted.");
			break;
		case ERROR_LOADING_DEVICES:
			MSG_ERROR("Error loading devices: check if adb is installed.");
			break;
		case ERROR_NO_DEVICES:
		default:
			MSG_ERROR("No devices detected:\n"
				"Re-attach device and try running `adb devices` in Terminal.");
			break;
	}
}


int CheckAdbDevices(int port) {
	char buf[256];
	FILE* pipe;
	int rc = system("adb start-server");
	if (WEXITSTATUS(rc) != 0){
		rc = ERROR_LOADING_DEVICES;
		goto EXIT;
	}

	pipe = popen("adb devices", "r");
	if (!pipe) {
		rc = ERROR_LOADING_DEVICES;
		goto EXIT;
	}

	rc = ERROR_NO_DEVICES;

	while (!feof(pipe)) {
		if (fgets(buf, sizeof(buf), pipe) == NULL) break;
		dbgprint("Got line: %s", buf);
		if (strstr(buf, "List of") != NULL){
			continue;
		}
		if (strstr(buf, "offline") != NULL){
			rc = ERROR_DEVICE_OFFLINE;
			if (system("adb kill-server") < 0){}
			break;
		}
		if (strstr(buf, "unauthorized") != NULL){
			rc = ERROR_DEVICE_NOTAUTH;
			break;
		}
		if (strstr(buf, "device") != NULL && strstr(buf, "??") == NULL){
			rc = NO_ERROR;
			break;
		}
	}
	pclose(pipe);

EXIT:
	dbgprint("CheckAdbDevices rc=%d\n", rc);

	if (rc == NO_ERROR) {
		snprintf(buf, sizeof(buf), "adb forward tcp:%d tcp:%d", port, port);
		rc = system(buf);
		if (WEXITSTATUS(rc) != 0){
			rc = ERROR_ADDING_FORWARD;
		}
	}

	return rc;
}

void iOSErrorPrint(int rc) {
	switch (rc) {
		case ERROR_LOADING_DEVICES:
			MSG_ERROR("Error loading devices:\n"
				"Make sure usbmuxd service is installed and running.");
			break;
		case ERROR_NO_DEVICES:
			MSG_ERROR("No devices detected:\n"
				"Make sure usbmuxd service running and this computer is trusted.");
			break;
		case ERROR_ADDING_FORWARD:
			MSG_ERROR("Error getting a connection:\n"
				"Make sure DroidCam app is open.\n"
				"Try re-attaching device.");
			break;
		default:
			errprint("unexpected rc=%d from CheckiOSDevices()\n", rc);
			break;
	}
}

int CheckiOSDevices(int port) {
	usbmuxd_device_info_t *deviceList = NULL;
	const int deviceCount = usbmuxd_get_device_list(&deviceList);
	dbgprint("CheckiOSDevices: found %d devices\n", deviceCount);

	if (deviceCount < 0) {
		return ERROR_LOADING_DEVICES;
	}
	if (deviceCount == 0) {
		usbmuxd_device_list_free(&deviceList);
		return ERROR_NO_DEVICES;
	}

	const int sfd = usbmuxd_connect(deviceList[0].handle, port);
	if (sfd <= 0) {
		usbmuxd_device_list_free(&deviceList);
		return ERROR_ADDING_FORWARD;
	}

	// remove the NONBLOCK flag
	int flags = fcntl(sfd, F_GETFL, NULL);
	flags &= ~O_NONBLOCK;
	fcntl(sfd, F_SETFL, flags);

	usbmuxd_device_list_free(&deviceList);
	return sfd;
}
