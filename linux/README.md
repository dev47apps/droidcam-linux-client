Linux Client
========

## Building

Download and install libjpeg-turbo binaries (should go into /opt/libjpeg-turbo):
https://github.com/libjpeg-turbo/libjpeg-turbo/releases

Install the following dependencies
```
gtk+-2.0
libavutil-dev
libswscale-dev
```

Run `make`

To install then, use either the `install` or `install-dkms` scripts [depending on the fact your system supports DKMS](./README-DKMS.md).
