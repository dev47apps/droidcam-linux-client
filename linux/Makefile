# DroidCam & DroidCamX (C) 2010
# https://github.com/aramg
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
# Use at your own risk. See README file for more details.

JPEG_DIR ?= /opt/libjpeg-turbo
JPEG_INCLUDE ?= $(JPEG_DIR)/include
JPEG_LIB ?= $(JPEG_DIR)/lib$(shell getconf LONG_BIT)

CC    = -no-pie
GTK   = `pkg-config --libs --cflags gtk+-2.0` `pkg-config --libs x11`
LIBS  = `pkg-config --libs --cflags libswscale libavutil`
JPEG  = -I$(JPEG_INCLUDE) $(JPEG_LIB)/libturbojpeg.a
SRC      = src/connection.c src/decoder.c
NO_WARN  = -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast

ifeq "$(RELEASE)" "1"
LIBS = /usr/lib/x86_64-linux-gnu/libswscale.a /usr/lib/x86_64-linux-gnu/libavutil.a -lpthread
package: clean all
	zip -x *.png src/* -r droidcam_`date +%s`.zip ./*

endif

all: droidcam-cli droidcam

droidcam-cli: src/droidcam-cli.c $(SRC)
	gcc -Wall $(CC) $(NO_WARN) $^        $(JPEG) $(LIBS) -lm -o droidcam-cli

droidcam: src/droidcam.c $(SRC)
	gcc -Wall $(CC) $(NO_WARN) $^ $(GTK) $(JPEG) $(LIBS) -lm -o droidcam

clean:
	rm droidcam || true
	rm droidcam-cli || true
	make -C v4l2loopback clean
