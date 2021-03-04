/* DroidCam & DroidCamX (C) 2010-2021
 * https://github.com/dev47apps
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "usbmuxd.h"

#include "common.h"
#include "settings.h"

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
		sprintf(buf, "adb forward tcp:%d tcp:%d", port, port);
		rc = system(buf);
		if (WEXITSTATUS(rc) != 0){
			rc = ERROR_ADDING_FORWARD;
			MSG_ERROR("Error adding adb forward!");
		}
	}
	else if (rc == ERROR_NO_DEVICES) {
		MSG_ERROR("No devices detected.\n"
			"Reconnect device and try running `adb devices` in Terminal.");
	}
	else if (rc == ERROR_DEVICE_OFFLINE) {
		system("adb kill-server");
		MSG_ERROR("Device is offline. Try re-attaching device.");
	}
	else if (rc == ERROR_DEVICE_NOTAUTH) {
		system("adb kill-server");
		MSG_ERROR("Device is in unauthorized state.");
	}
	else {
		MSG_ERROR("Error loading devices.\n"
			"Make sure adb is installed and try running `adb devices` in Terminal.");
	}

	return rc;
}

// free-ing this list causes a connection reset on some systems
usbmuxd_device_info_t *deviceList = NULL;
int deviceCount = 0;

int CheckiOSDevices(int port) {
	if (!deviceList) {
		deviceCount = usbmuxd_get_device_list(&deviceList);
		dbgprint("CheckiOSDevices: found %d devices\n", deviceCount);
	}
	if (deviceCount < 0) {
		MSG_ERROR("Error loading devices.\n"
			"Make sure usbmuxd service is installed and running.");
		return ERROR_LOADING_DEVICES;
	}
	if (deviceCount == 0) {
		MSG_ERROR("No devices detected.\n"
			"Make sure usbmuxd service running and this computer is trusted.");
		return ERROR_NO_DEVICES;
	}

	int rc = usbmuxd_connect(deviceList[0].handle, (short) port);
	if (rc <= 0) {
		dbgprint("usbmuxd_connect failed: %d\n", rc);
		rc = ERROR_ADDING_FORWARD;
		MSG_ERROR("Error getting a connection.\n"
			"Make sure DroidCam app is open,\nor try re-attaching device.");
	}

	return rc;
}

void FreeUSB() {
	if (deviceList) usbmuxd_device_list_free(&deviceList);
	deviceList = NULL;
	deviceCount = 0;
}
