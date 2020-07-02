/* DroidCam & DroidCamX (C) 2010-
 * https://github.com/aramg
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * Use at your own risk. See README file for more details.
 */

#ifndef _COMMON_H_
#define _COMMON_H_

#define APP_VER_INT 140
#define APP_VER_STR "1.4"

#define MSG_ERROR(str)     ShowError("Error",str)
#define MSG_LASTERROR(str) ShowError(str,strerror(errno))
void ShowError(const char*, const char*);

#define ADB_LOCALHOST_IP "127.0.0.1"

#define VIDEO_REQ "CMD /v2/video.4?%dx%d"
#define OTHER_REQ "CMD /v1/ctl?%d"

#define AUDIO_REQ "CMD /v2/audio"
#define STOP_REQ  "CMD /v1/stop"

#define PING_REQ "CMD /ping"

#define CSTR_LEN(x) (sizeof(x)-1)

#define make_int(num, b1, b2)	num = 0; num |=(b1&0xFF); num <<= 8; num |= (b2&0xFF);
#define make_int4(num, b0, b1, b2, b3) \
    num = 0; \
    num |= (b3&0xFF); num <<= 8; \
    num |= (b2&0xFF); num <<= 8; \
    num |= (b1&0xFF); num <<= 8; \
    num |= (b0&0xFF)

#define errprint(...) fprintf(stderr, __VA_ARGS__)
#define voidprint(...) /* */
#define dbgprint      voidprint

#define VIDEO_INBUF_SZ 4096
#define AUDIO_INBUF_SZ 32

#ifndef FALSE
# define FALSE 0
#endif

#ifndef TRUE
# define TRUE  1
#endif

#endif
