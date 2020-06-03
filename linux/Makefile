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

CC    = -no-pie
GTK   = `pkg-config --libs --cflags gtk+-3.0` `pkg-config --libs x11`
LIBAV = `pkg-config --libs --cflags libswscale libavutil`
LIBS  =  -lspeex -lasound -lpthread -lm
JPEG  = -I$(JPEG_INCLUDE) $(JPEG_LIB)/libturbojpeg.a
SRC      = src/connection.c src/settings.c src/decoder*.c src/av.c src/adb.c

all: droidcam-cli droidcam

ifeq "$(RELEASE)" "1"
LIBAV = /usr/lib/x86_64-linux-gnu/libswscale.a /usr/lib/x86_64-linux-gnu/libavutil.a
package: clean all
	zip -x *.png src/* -r droidcam_`date +%s`.zip ./*
endif

gresource: .gresource.xml icon2.png
	glib-compile-resources .gresource.xml --generate-source --target=src/resources.c

droidcam-cli: src/droidcam-cli.c $(SRC)
	gcc -Wall $(CC) $^ $(JPEG) $(LIBAV) $(LIBS) -o droidcam-cli

droidcam: src/droidcam.c src/resources.c $(SRC)
	gcc -Wall $(CC) $^ $(GTK) $(JPEG) $(LIBAV) $(LIBS) -o droidcam

clean:
	rm droidcam || true
	rm droidcam-cli || true
	make -C v4l2loopback clean
