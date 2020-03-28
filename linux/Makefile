# DroidCam & DroidCamX (C) 2010
# https://github.com/aramg
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
# Use at your own risk. See README file for more details.

GTK   = `pkg-config --libs --cflags gtk+-2.0`
JPEG  = -I/opt/libjpeg-turbo/include /opt/libjpeg-turbo/lib`getconf LONG_BIT`/libturbojpeg.a
LIBS  = -lswscale

SRC      = src/connection.c src/decoder.c
NO_WARN  = -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast

all: droidcam-cli droidcam

droidcam-cli: src/droidcam-cli.c $(SRC)
	gcc -Wall $(NO_WARN) $^        $(JPEG) $(LIBS) -lm -o droidcam-cli

droidcam: src/droidcam.c $(SRC)
	gcc -Wall $(NO_WARN) $^ $(GTK) $(JPEG) $(LIBS) -lm -o droidcam

clean:
	rm droidcam || true
	rm droidcam-cli || true
	make -C v4l2loopback clean
