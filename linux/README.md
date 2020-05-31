Linux Client
========

## Building

Download and install the official libjpeg-turbo binaries, i.e. `libjpeg-turbo-official_2.0.4_amd64.deb` or —`.rpm` (as appropriate for your distribution) by visiting the [2.0.4 release](https://github.com/libjpeg-turbo/libjpeg-turbo/releases) GitHub page and following the SourceForge link.

The files should automatically install into `/opt/libjpeg-turbo`.

Install the following dependencies
```
gtk+-2.0
libavutil-dev
libswscale-dev
libasound2-dev
libspeex-dev
dkms  (if supported)
```

Run `make`

To install, as superuser (i.e. with the `sudo` command) use either the `install` or `install-dkms` script [depending on whether or not your system supports DKMS](./README-DKMS.md).
