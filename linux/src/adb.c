#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"

int CheckAdbDevices(int port) {
	char buf[256];
	int haveDevice = 0;

	system("adb start-server");
	FILE* pipe = popen("adb devices", "r");
	if (!pipe) {
		goto _exit;
	}

	while (!feof(pipe)) {
		dbgprint("->");
		if (fgets(buf, sizeof(buf), pipe) == NULL) break;
		dbgprint("Got line: %s", buf);

		if (strstr(buf, "List of") != NULL){
			haveDevice = 2;
			continue;
		}
		if (haveDevice == 2) {
			if (strstr(buf, "offline") != NULL){
				haveDevice = 4;
				break;
			}
			if (strstr(buf, "device") != NULL && strstr(buf, "??") == NULL){
				haveDevice = 8;
				break;
			}
		}
	}
	pclose(pipe);
	#define TAIL "Please refer to the website for manual adb setup info."
	if (haveDevice == 0 || haveDevice == 1) {
		MSG_ERROR("adb program not detected. " TAIL);
	}
	else if (haveDevice == 2) {
		MSG_ERROR("No devices detected. " TAIL);
	}
	else if (haveDevice == 4) {
		system("adb kill-server");
		MSG_ERROR("Device is offline. Try re-attaching device.");
	}
	else if (haveDevice == 8) {
		sprintf(buf, "adb forward tcp:%d tcp:%d", port, port);
		system(buf);
	}
_exit:
	dbgprint("haveDevice = %d\n", haveDevice);
	return haveDevice;
}
