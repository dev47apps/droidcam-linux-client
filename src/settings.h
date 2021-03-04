// Copyright (C) 2020 github.com/dev47apps
#ifndef _SETTINGS_H_
#define _SETTINGS_H_
extern "C" {
enum radios {
    CB_RADIO_WIFI,
    CB_RADIO_ADB,
    CB_RADIO_IOS,
    CB_WIFI_SRVR,
    CB_H_FLIP,
    CB_V_FLIP,
    CB_RADIO_COUNT
};

enum widgets {
    CB_BUTTON = CB_RADIO_COUNT,
    CB_AUDIO,
    CB_VIDEO,
    CB_BTN_OTR,
    CB_WIDGETS_COUNT
};

enum control_code {
    CB_CONTROL_ZIN = 16,
    CB_CONTROL_ZOUT,
    CB_CONTROL_AF,
    CB_CONTROL_LED,
};

struct settings {
    char ip[32];
    int port;
    int audio;
    int video;
    int connection; // Connection type
    unsigned v4l2_width, v4l2_height;

    int confirm_close;
};
}
void LoadSettings(struct settings* settings);
void SaveSettings(struct settings* settings);

#define NO_ERROR 0
#define ERROR_NO_DEVICES      -1
#define ERROR_LOADING_DEVICES -2
#define ERROR_ADDING_FORWARD  -3
#define ERROR_DEVICE_OFFLINE  -4
#define ERROR_DEVICE_NOTAUTH  -5
int CheckAdbDevices(int port);
int CheckiOSDevices(int port);
void FreeUSB();

#endif
