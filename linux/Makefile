# DroidCam & DroidCamX (C) 2010
# https://github.com/aramg
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
# Use at your own risk. See README file for more details.

ifeq "$(RELEASE)" "1"
PC_STATIC = --static
LINK_NEXT_STATICALLY = -Wl,-Bstatic
LINK_REST_DYNAMICALLY = -Wl,-Bdynamic
endif

GXX   = g++
CC    = -std=c++11 -x c++ -Wall -fPIC -no-pie
GTK   = `pkg-config --libs --cflags gtk+-3.0` `pkg-config --libs x11`
LIBAV = `pkg-config --libs --cflags libswscale libavutil` # Do not use $(PC_STATIC) here since forces us to link against ton of unnecessary libraries that are not available statically on Ubuntu.
LIBS  = `pkg-config --libs --cflags speex alsa` -lpthread -lm
JPEG  = `pkg-config $(PC_STATIC) --libs --cflags 'libturbojpeg >= 2.0.5'`
SRC      = src/connection.c src/settings.c src/decoder*.c src/av.c src/usb.c
USBMUXD = `pkg-config $(PC_STATIC) --libs --cflags libusbmuxd`

all: droidcam-cli droidcam

ifeq "$(RELEASE)" "1"
package: clean all
	zip -x *.png src/ src/* Makefile -r droidcam_`date +%s`.zip ./*
endif

gresource: .gresource.xml icon2.png
	glib-compile-resources .gresource.xml --generate-source --target=src/resources.c

droidcam-cli: src/droidcam-cli.c $(SRC)
	$(GXX) $(CC) $^ $(LINK_NEXT_STATICALLY) $(JPEG) $(LIBAV) $(USBMUXD) $(LINK_REST_DYNAMICALLY) $(LIBS) -o droidcam-cli

droidcam: src/droidcam.c src/resources.c $(SRC)
	$(GXX) $(CC) $^ $(LINK_NEXT_STATICALLY) $(JPEG) $(LIBAV) $(USBMUXD) $(LINK_REST_DYNAMICALLY) $(GTK) $(LIBS) -o droidcam

clean:
	rm droidcam || true
	rm droidcam-cli || true
	make -C v4l2loopback clean
