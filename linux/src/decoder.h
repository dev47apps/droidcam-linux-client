/* DroidCam & DroidCamX (C) 2010-
 * Author: Aram G. (dev47@dev47apps.com)
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * Use at your own risk. See README file for more details.
 */
#ifndef __DECODR_H__
#define __DECODR_H__

// Global initialization 
int  decoder_init();
void decoder_fini();

// Single session initialization
int  decoder_prepare_video(char * header);
void decoder_cleanup();

int DecodeVideo(char * data, int length);
int DecodeAudio(char * data, int length);

int GetVideoWidth();
int GetVideoHeight();

#define VIDEO_FMT_YUV  1
#define VIDEO_FMT_H263 2
#define VIDEO_FMT_JPEG 3

#endif
