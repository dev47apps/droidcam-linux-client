Linux Client
========

## Download and Install

You can download and install the latest release from the official website at https://www.dev47apps.com/droidcam/linuxx/, along with instructions on how to update the webcam resolution and other info.

All releases are also available here on GitHub at https://github.com/aramg/droidcam/releases

## Building

Download and install latest libjpeg-turbo 2.0.X via:
https://github.com/libjpeg-turbo/libjpeg-turbo/releases

The libjpeg-turbo package should go into `/opt/libjpeg-turbo`.
The official binaries (.deb, .rpm) will automatically install into the correct directory.

Install the following dependencies
```
gtk+-3.0        # Only needed for GUI client
libavutil-dev
libswscale-dev
libasound2-dev
libspeex-dev
libusbmuxd-dev
libplist-dev
```

Set path to the libjpeg’s pkg-config directory, for example using `export PKG_CONFIG_PATH=/opt/libjpeg-turbo/lib64/pkgconfig`.

Run `make`, or `make droidcam-cli` if you skipped installing GTK+, to build the droidcam binaries.

To install, run `sudo ./install`, or, `sudo ./install-dkms` [if your system supports DKMS](./README-DKMS.md).
