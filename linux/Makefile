# DroidCam & DroidCamX (C) 2010
# Author: Aram G. (dev47@dev47apps.com)
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
# Use at your own risk. See README file for more details.

GTK  = `pkg-config --libs --cflags gtk+-2.0`
LIBS     = -lgthread-2.0 -lbluetooth -Lffmpeg/ -lavcodec  -lavcore  -lavutil  -lswscale -lopencore-amrnb
SRC      = src/droidcam.c src/connection.c src/decoder.c
SRC_CLI  = src/droidcam-cli.c src/connection.c src/decoder.c

all:
	gcc -Wall -Iffmpeg/include $(SRC) $(LIBS) $(GTK) -o droidcam

cli:
	gcc -Wall -Iffmpeg/include $(SRC_CLI) $(LIBS) -lm -o droidcam-cli

clean:
	rm droidcam || true
	rm droidcam-cli || true
	cd webcam && make -C /lib/modules/`uname -r`/build M=`pwd` clean
	cd v4l2loopback && make -C /lib/modules/`uname -r`/build M=`pwd` clean
