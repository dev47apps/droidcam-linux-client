# DroidCam & DroidCamX (C) 2010
# https://github.com/aramg
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
# Use at your own risk. See README file for more details.

JPEG_DIR ?= /opt/libjpeg-turbo
JPEG_INCLUDE ?= $(JPEG_DIR)/include
JPEG_LIB ?= $(JPEG_DIR)/lib`getconf LONG_BIT`

GXX   = g++
CC    = -std=c++11 -x c++ -Wall -fPIC -no-pie
GTK   = `pkg-config --libs --cflags gtk+-3.0` `pkg-config --libs x11`
GTK  += `pkg-config --cflags --libs appindicator3-0.1`
LIBAV = `pkg-config --libs --cflags libswscale libavutil`
LIBS  =  -lspeex -lasound -lpthread -lm
JPEG  = -I$(JPEG_INCLUDE) $(JPEG_LIB)/libturbojpeg.a
SRC      = src/connection.c src/settings.c src/decoder*.c src/av.c src/usb.c
USBMUXD = -lusbmuxd

all: droidcam-cli droidcam

ifeq "$(RELEASE)" "1"
LIBAV = /usr/lib/x86_64-linux-gnu/libswscale.a /usr/lib/x86_64-linux-gnu/libavutil.a
SRC  += /usr/lib/x86_64-linux-gnu/libusbmuxd.a /usr/lib/x86_64-linux-gnu/libxml2.a src/libplist-2.0.a
CC += -DIS_OFFICIAL_BUILD=1
package: clean all
	zip -x icon.png src/ src/* Makefile -r droidcam_`date +%s`.zip ./*

else
LIBS += $(USBMUXD)
ifneq "$(DISTRO_SUPPORT_LINK)" ""
CC += -DIS_OFFICIAL_BUILD=0 '-DDISTRO_SUPPORT_LINK="$(DISTRO_SUPPORT_LINK)"'
endif
endif

gresource: .gresource.xml icon2.png
	glib-compile-resources .gresource.xml --generate-source --target=src/resources.c

droidcam-cli: src/droidcam-cli.c $(SRC)
	$(GXX) $(CC) $^ $(JPEG) $(LIBAV) $(LIBS) -o droidcam-cli

droidcam: src/droidcam.c src/resources.c $(SRC)
	$(GXX) $(CC) $^ $(GTK) $(JPEG) $(LIBAV) $(LIBS) -o droidcam

clean:
	rm droidcam || true
	rm droidcam-cli || true
	make -C v4l2loopback clean
