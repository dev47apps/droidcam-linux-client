/* DroidCam & DroidCamX (C) 2010-
 * Author: Aram G. (dev47@dev47apps.com)
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * Use at your own risk. See README file for more details.
 */
#ifndef _COMMON_H_
#define _COMMON_H_

#define MSG_ERROR(str)     ShowError("Error",str)
#define MSG_LASTERROR(str) ShowError(str,strerror(errno))

extern void ShowError();

#define OTHER_REQ "COMMAND /other %d" 
#define VIDEO_REQ "COMMAND /videre %dx%d" 
#define AUDIO_REQ "COMMAND /audire"

//#define dbgprint(...) printf(__VA_ARGS__); fflush(stdout);
#define dbgprint(...) 

#define YUV_BUFFER_SZ(w,h) (w*h*3/2)
#define RGB_BUFFER_SZ(w,h) (w*h*3)

#define VIDEO_INBUF_SZ 4096

#endif
