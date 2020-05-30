Linux Client
========

## Building

Download and install latest libjpeg-turbo 2.0.X via:
https://github.com/libjpeg-turbo/libjpeg-turbo/releases

The libjpeg-turbo package should go into `/opt/libjpeg-turbo`.
The official binaries (.deb, .rpm) will automatically install into the correct directory.

Install the following dependencies
```
gtk+-3.0
glib-2.0
libavutil-dev
libswscale-dev
libasound2-dev
libspeex-dev
```

Run `make`

To install, run `sudo ./install`, or, `sudo ./install-dkms` [if your system supports DKMS](./README-DKMS.md).
