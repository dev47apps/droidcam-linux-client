# DroidCam & DroidCamX (C) 2010
# https://github.com/aramg
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
# Use at your own risk. See README file for more details.

GTK   = `pkg-config --libs --cflags gtk+-2.0`
LIBS     = -lgthread-2.0 -l:/usr/lib/libswscale.a  -l:/usr/lib/libavutil.a -l:/opt/libjpeg-turbo/lib`getconf LONG_BIT`/libturbojpeg.a
CC       =
SRC      = src/connection.c src/decoder.c

all:
	gcc -Wall $(CC) $(SRC) src/droidcam.c $(LIBS) $(GTK) -lm -o droidcam

cli:
	gcc -Wall $(CC) $(SRC) src/droidcam-cli.c $(LIBS) -lm -o droidcam-cli

clean:
	rm droidcam || true
	rm droidcam-cli || true
	cd v4l2loopback && make -C /lib/modules/`uname -r`/build M=`pwd` clean
