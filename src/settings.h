// Copyright (C) 2020 github.com/dev47apps
#ifndef _SETTINGS_H_
#define _SETTINGS_H_

#include <netinet/in.h>
#include <stdbool.h>
#include <stdint.h>

enum radios {
    CB_RADIO_WIFI,
    CB_RADIO_ADB,
    CB_RADIO_IOS,
    CB_WIFI_SRVR,
    CB_RADIO_COUNT
};

enum widgets {
    CB_BUTTON = CB_RADIO_COUNT,
    CB_AUDIO,
    CB_VIDEO,
    CB_BTN_EL,
    CB_BTN_WB,
    CB_BTN_OTR,
    CB_WIDGETS_COUNT
};


enum control_codes {
    CB_CONTROL_EMPTY_0 = 0,
    CB_CONTROL_EL_OFF,
    CB_CONTROL_EL_ON,
    CB_CONTROL_WB,
    CB_CONTROL_EV,
    CB_CONTROL_EMPTY_5,
    CB_CONTROL_ZOOM_IN,
    CB_CONTROL_ZOOM_OUT,
    CB_CONTROL_AF,
    CB_CONTROL_LED,
    CB_H_FLIP,
    CB_V_FLIP,
};

struct settings {
    char ip[INET_ADDRSTRLEN];
    uint16_t port;
    int audio;
    int video;
    int connection; // Connection type
    unsigned v4l2_width, v4l2_height;

    int confirm_close;
    int horizontal_flip;
    int vertical_flip;
};

void LoadSettings(struct settings* settings);
void SaveSettings(struct settings* settings);

#define NO_ERROR 0
#define ERROR_NO_DEVICES      -1
#define ERROR_LOADING_DEVICES -2
#define ERROR_ADDING_FORWARD  -3
#define ERROR_DEVICE_OFFLINE  -4
#define ERROR_DEVICE_NOTAUTH  -5
int CheckAdbDevices(uint16_t port);
int CheckiOSDevices(uint16_t port);

#endif
