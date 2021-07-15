DKMS for `v4l2loopback_dc`
=========================

The [DKMS mechanism][DKMS] is a convenient way to have extra Linux kernel modules managed outside of the kernel tree source to _survive_ kernel updates.

Once the `v4l2loopback_dc` module, which is necessary for [Droidcam] to work, has been built and installed, you may face at some point that after a system update, implying a kernel update, [Droidcam] may not work anymore... when running `droidcam`, it may fail starting with the following message:

```
Device not found (/dev/video[0-9]).
Did you install it?
```

This is just because the newly installed kernel does not come with the `v4l2loopback_dc` module, thus you would then have to re-install it manually again. You may have forgotten what you did the first time, have to dig again into documentation, how you did it on the first place...

This is where [DKMS] comes into the picture.

By properly declaring the `v4l2loopback_dc` module as a [DKMS] module, future installs of kernel upgrades will _automatically_ take `v4l2loopback_dc` module re-installation into account after the kernel has been updated.

**If your system supports DKMS, it should probably be your prefered install mechanism.** Both for the fact it survives kernel updates, but also for the fact it keeps your kernel module tree _clean_, as extra [DKMS] modules are kept in separated directories.

# DKMS flavour installation of `droidcam`

First clone the `droidcam` Github repo anywhere you want (in `/opt` for example):

    $ git clone https://github.com/dev47apps/droidcam.git

Build it following the standard procedure described [here][droidcam build procedure].

**:information_source: The pre-requisite for what's coming next is that the previous build succeeded.**


From within the repository, go to the `linux` directory, and then issue a:

    $ sudo ./install-dkms [width] [height]

If you want specific webcam resolution, you can directly pass the width and height to the script (as for the standard install script). Default is 640 480.

:information_source: After this, the module is built, loaded (you may check this using `lsmod|grep v4l2loopback_dc`), and its config for the webcam resolution is created in the file `/etc/modprobe.d/droidcam.conf` (you may want to edit this file afterwards, or you can re-run the install script multiple times with different parameters which is harmless). [Supported webcam resolutions are listed here][webcam resolutions].


# Uninstalling `droidcam` after a DKMS install


From within the repository, go to the `linux` directory, and then issue a:

    $ sudo ./uninstall-dkms

Or alternatively, in case you removed the original repository after install, you can issue:

    $ sudo /opt/droidcam-uninstall


[DKMS]: https://github.com/dell/dkms "DKMS source code page on Github"
[Droidcam]: https://github.com/dev47apps/droidcam "Droid source code page on Github"
[droidcam build procedure]: https://github.com/dev47apps/droidcam/tree/master/linux "droidcam build procedure"
[webcam resolutions]: http://www.dev47apps.com/droidcam/linux/ "Supported webcams resolutions"

