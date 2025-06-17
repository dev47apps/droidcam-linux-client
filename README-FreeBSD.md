FreeBSD
========

## Initial considerations

Please make sure that you add your user to the webcamd group after the installation of the dependencies, this can be done by running `doas pw groupmod webcamd -m $USER`

You will also want to enable webcamd and the cuse module to load at boot for that you'll need to modify __/etc/rc.conf__ and __/boot/loader.conf__ with:

>webcamd_enable="YES"

and

>cuse_load="YES"

respectively.

## Getting all the necessary dependencies

Run `doas pkg install gmake gcc pkgconf libjpeg-turbo usbmuxd libusbmuxd alsa-lib v4l_compat speex ffmpeg webcamd libappindicator`

## Building and Installing

Run `gmake`, or `gmake droidcam-cli` if you wish to only build the command line version of droidcam.

To install, run `doas ./install-client`

## Running

You'll need to run `doas webcamd -B -c v4l2loopback` before launching droidcam
