Linux Client
========

## Download and Install

You can download and install the latest release from the official website at https://www.dev47apps.com/droidcam/linux/, along with instructions on how to update the webcam resolution and other info.

Releases are also available here on GitHub at https://github.com/dev47apps/droidcam/releases

Raspberry-PI instructions can be found here: https://github.com/dev47apps/droidcam/wiki/Raspberry-PI

## Building

Download and install latest libjpeg-turbo release via
https://github.com/libjpeg-turbo/libjpeg-turbo/releases

The libjpeg-turbo package should go into `/opt/libjpeg-turbo`.
The official binaries (.deb, .rpm) will automatically install into the correct directory.

Install the following dependencies
(the package names are for Debian based systems, adjust as needed for other distros)
```
libavutil-dev
libswscale-dev
libasound2-dev
libspeex-dev
libusbmuxd-dev
libplist-dev

gtk+-3.0               # Only needed for GUI client
libappindicator3-dev   # Only needed for GUI client^^

```

Run `make`, or `make droidcam-cli` if you skipped installing GTK+, to build the droidcam binaries.

To install, run `sudo ./install-client`

^^ Some distros are removing libappindicator in their latest versions (Ubuntu 21+, Fedora 33+, Debian Bullseye+), which is used for system tray icon.
The new dependency is `libayatana-appindicator3-dev`

Building:

`APPINDICATOR=ayatana-appindicator3-0.1 make droidcam`


## V4L2 Loopback (Webcam driver)

DroidCam has its own version of v4l2loopback, v4l2loopback-dc, which makes the app a little more user-friendly.
It also works with the standard v4l2loopback module, so installing v4l2loopback-dc is optional.

The standard v4l2loopback module is already available on most distros as v4l2loopback-dkms. See [v4l2loopback usage examples](https://github.com/dev47apps/droidcam/releases/tag/v1.7).

With v4l2loopback-dc you’ll see "DroidCam" in the list of webcams, it works with Skype+Chrome without needing exclusive_caps=1, and the install scripts will make sure v4l2loopback-dc stays loaded after reboot.

To install v4l2loopback-dc, make sure you have these dependencies installed
```
linux-headers-`uname -r` gcc make
```
then run `sudo ./install-video`.

Debian/Ubuntu and RHEL (Fedora/SUSE) based distros:

[If your system supports DKMS](./README-DKMS.md), you can instead use `sudo ./install-dkms`.

## Sound

DroidCam can use the Linux ALSA Loopback sound card for audio. There are many differences and quirks with the audio layers on different Linux systems. It’s recommended you use a regular microphone and keep droidcam for video only.

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
