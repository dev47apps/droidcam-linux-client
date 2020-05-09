#ifndef _SETTINGS_H_
#define _SETTINGS_H_

enum radios {
    CB_RADIO_WIFI,
    CB_RADIO_ADB,
    CB_WIFI_SRVR,
    CB_RADIO_COUNT
};

enum widgets {
    CB_BUTTON = CB_RADIO_COUNT,
    CB_AUDIO,
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
    char ip[16];
    int port;
    int audio;
    int connection; // Connection type
};

void LoadSettings(struct settings* settings);
void SaveSettings(struct settings* settings);

int CheckAdbDevices(int port);

#endif
