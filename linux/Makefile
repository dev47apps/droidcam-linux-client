# DroidCam & DroidCamX (C) 2010
# Author: Aram G. (dev47@dev47apps.com)
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
# Use at your own risk. See README file for more details.

GTK   = `pkg-config --libs --cflags gtk+-2.0`
WAND  = `pkg-config --cflags --libs MagickWand`
LIBS     = -lgthread-2.0 /opt/libjpeg-turbo/lib64/libturbojpeg.a
CC       = 
SRC      = src/droidcam.c src/connection.c src/decoder.c
SRC_CLI  = src/droidcam-cli.c src/connection.c src/decoder.c

all:
	gcc -Wall $(CC) $(SRC) $(LIBS) $(GTK) -o droidcam

cli:
	gcc -Wall $(CC) $(SRC_CLI) $(WAND)  $(LIBS) -lm -o droidcam-cli

clean:
	rm droidcam || true
	rm droidcam-cli || true
	cd v4l2loopback && make -C /lib/modules/`uname -r`/build M=`pwd` clean
