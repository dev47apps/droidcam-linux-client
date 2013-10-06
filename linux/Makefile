# DroidCam & DroidCamX (C) 2010
# Author: Aram G. (dev47@dev47apps.com)
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
# Use at your own risk. See README file for more details.

GTK  = `pkg-config --libs --cflags gtk+-2.0`
LIBS = -lbluetooth  -Lffmpeg/ -lavcodec  -lavcore  -lavutil  -lswscale -lopencore-amrnb
SRC  = src/droidcam.c src/connection.c src/decoder.c

all:
	gcc -Wall -Iffmpeg/include $(SRC) $(LIBS) $(GTK) -o droidcam

clean:
	rm droidcam
	cd webcam && make -C /lib/modules/`uname -r`/build M=`pwd` clean
