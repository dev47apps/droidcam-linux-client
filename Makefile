# DroidCam & DroidCamX (C) 2010-2021
# https://github.com/dev47apps
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
# Use at your own risk. See README file for more details.

#
# Variables with ?= can be changed during invocation
# Example:
#  APPINDICATOR=ayatana-appindicator3-0.1 make droidcam


CC           ?= gcc
CFLAGS       ?= -Wall -O2
APPINDICATOR ?= appindicator3-0.1

GTK   = `pkg-config --libs --cflags gtk+-3.0` `pkg-config --libs x11`
GTK  += `pkg-config --libs --cflags $(APPINDICATOR)`
LIBAV = `pkg-config --libs --cflags libswscale libavutil`
JPEG  = `pkg-config --libs --cflags libturbojpeg`
USBMUXD = `pkg-config --libs --cflags libusbmuxd`
LIBS  = -lspeex -lasound -lpthread -lm
SRC   = src/connection.c src/settings.c src/decoder*.c src/av.c src/usb.c src/queue.c

ifneq ($(findstring ayatana,$(APPINDICATOR)),)
	CFLAGS += -DUSE_AYATANA_APPINDICATOR
endif

all: droidcam-cli droidcam

ifeq "$(RELEASE)" ""
package:
	@echo "usage: RELEASE=2. make package"

else
JPEG    = -I/opt/libjpeg-turbo/include
USBMUXD = -I/opt/libimobiledevice/include
LIBAV   = -L/opt/ffmpeg4/lib -lswscale -lavutil

SRC += /opt/libimobiledevice/lib/libusbmuxd.a
SRC += /opt/libimobiledevice/lib/libplist-2.0.a
SRC += /opt/libjpeg-turbo/lib64/libturbojpeg.a

.PHONY: package
package: all
	zip "droidcam_$(RELEASE).zip" \
		LICENSE README* icon2.png  \
		droidcam* install* uninstall* \
		v4l2loopback/*
endif

#src/resources.c: .gresource.xml icon2.png
#	glib-compile-resources .gresource.xml --generate-source --target=src/resources.c

droidcam-cli: LDLIBS +=        $(LIBAV) $(JPEG) $(USBMUXD) $(LIBS)
droidcam:     LDLIBS += $(GTK) $(LIBAV) $(JPEG) $(USBMUXD) $(LIBS)

droidcam-cli: src/droidcam-cli.c $(SRC)
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ -o $@ $(LDFLAGS) $(LDLIBS)

droidcam: src/droidcam.c src/resources.c $(SRC)
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ -o $@ $(LDFLAGS) $(LDLIBS)

clean:
	rm -f droidcam
	rm -f droidcam-cli
	make -C v4l2loopback clean
