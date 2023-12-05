Linux
========

## Download and Install

You can download and install the latest release from the official website at https://www.dev47apps.com/droidcam/linux/, along with instructions on how to update the webcam resolution and other info.

Releases are also available here on GitHub at https://github.com/dev47apps/droidcam/releases

Raspberry-PI instructions can be found here: https://github.com/dev47apps/droidcam/wiki/Raspberry-PI

## Building

Install the dependencies

```
Debian/ubuntu:
libavutil-dev libswscale-dev libasound2-dev libspeex-dev libusbmuxd-dev libplist-dev libturbojpeg0-dev

# Only needed for GUI client
libgtk-3-dev libappindicator3-dev


Fedora:
libavutil-free-devel libswscale-free-devel alsa-lib-devel speex-devel libusbmuxd-devel libplist-devel turbojpeg-devel

# Only needed for GUI client
gtk3-devel libappindicator-gtk3-devel
```

Run `make`, or `make droidcam-cli` if you skipped installing GTK+, to build the droidcam binaries.

To install, run `sudo ./install-client`


Note: Some distros are removing libappindicator in their latest versions (Ubuntu 21+, Fedora 33+, Debian Bullseye+), used for system tray icon.
The new dependency (Ubuntu) is `libayatana-appindicator3-dev`

You can specify the indicator libary to make like so:
`APPINDICATOR=ayatana-appindicator3-0.1 make droidcam`


## V4L2 Loopback (Webcam driver)

DroidCam has its own version of v4l2loopback, `v4l2loopback-dc`, which makes the app a little more user-friendly.
DroidCam works with the standard v4l2loopback module, so installing `v4l2loopback-dc` is optional.

The standard v4l2loopback module is already available on most distros as v4l2loopback-dkms. See [v4l2loopback usage examples](https://github.com/dev47apps/droidcam/releases/tag/v1.7).

The main differences with `v4l2loopback-dc` are that:
* You’ll see "DroidCam" in the list of webcams.
* It works with Skype+Chrome without the need for `exclusive_caps=1`.
* The install scripts will configure v4l2loopback-dc to auto-load after reboot.

To use v4l2loopback-dc, make sure you have these dependencies installed
```
linux-headers-`uname -r` gcc make
```
then run `sudo ./install-video` to build the module and install it.

Debian/Ubuntu and RHEL (Fedora/SUSE) based distros:
[If your system supports DKMS](./README-DKMS.md), you can instead use `sudo ./install-dkms`.

(note: you may need the `deb-helper` package)

## Sound

DroidCam can use the Linux ALSA Loopback sound card for audio.
There are many differences and quirks with audio on different Linux systems.
It’s recommended you use a regular microphone and keep droidcam for video only.

Run `sudo ./install-sound` to load the Linux ALSA Loopback sound card which the Droidcam client will use for audio input.

To get the mic to show up in PulseAudio you can either run `pacmd load-module module-alsa-source device=hw:Loopback,1,0` (you may need to adjust the last number),
or by editing /etc/pulse/default.pa [as described here](https://wiki.archlinux.org/index.php/PulseAudio/Troubleshooting#Microphone).
On some systems you need to do this after launching the droidcam client.

If the Loopback card takes over your line out, you can set the default PulseAudio sink as shown here: https://askubuntu.com/a/14083

To use DroidCam with Pipewire ([Source](https://gitlab.freedesktop.org/pipewire/pipewire/-/issues/713))
* Open pavucontrol, Configuration tab
* There's probably multiple devices called "Built-in Audio", one of them is droidcam. Try with the bottom device maybe.
* Choose the profile Pro Audio
* Go to the Input Devices tab
* Check which VU meter reacts to the phone's audio input (eg. Built-in Audio Pro 1), this is the desired audio input device.
* Inside pavucontrol you can now set this device as default input or choose it as the input device for individual apps etc.


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
