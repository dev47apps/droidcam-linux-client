/* DroidCam & DroidCamX (C) 2010-2021
 * https://github.com/dev47apps
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 */

#include "common.h"
#include "decoder.h"


#define AUDIO_RATE     16000
#define AUDIO_CHANNELS     1
#define PERIOD_TIME    (DROIDCAM_CHUNK_MS_2 * 1000)

char snd_device[32];

static snd_pcm_format_t format = SND_PCM_FORMAT_S16;

static snd_pcm_sframes_t buffer_size;
static snd_pcm_sframes_t period_size;

static unsigned int period_time = PERIOD_TIME;/* period time in us */
static unsigned int buffer_time = DROIDCAM_SPEEX_BACKBUF_MAX_COUNT * PERIOD_TIME; /* ring buffer length in us */

static int set_hwparams(snd_pcm_t *handle, snd_pcm_hw_params_t *params, snd_pcm_access_t access) {
    unsigned int rrate;
    snd_pcm_uframes_t size;
    int err;
    int dir = 0;
    const unsigned int resample = 1;
    /* choose all parameters */
    err = snd_pcm_hw_params_any(handle, params);
    if (err < 0) {
        errprint("Broken configuration for playback: no configurations available: %s\n", snd_strerror(err));
        return err;
    }
    /* set hardware resampling */
    err = snd_pcm_hw_params_set_rate_resample(handle, params, resample);
    if (err < 0) {
        errprint("Resampling setup failed for playback: %s\n", snd_strerror(err));
        return err;
    }
    /* set the interleaved read/write format */
    err = snd_pcm_hw_params_set_access(handle, params, access);
    if (err < 0) {
        errprint("Access type not available for playback: %s\n", snd_strerror(err));
        return err;
    }
    /* set the sample format */
    err = snd_pcm_hw_params_set_format(handle, params, format);
    if (err < 0) {
        errprint("Sample format not available for playback: %s\n", snd_strerror(err));
        return err;
    }
    /* set the count of channels */
    err = snd_pcm_hw_params_set_channels(handle, params, AUDIO_CHANNELS);
    if (err < 0) {
        errprint("Channels count (%u) not available for playbacks: %s\n", AUDIO_CHANNELS, snd_strerror(err));
        return err;
    }
    /* set the stream rate */
    rrate = AUDIO_RATE;
    err = snd_pcm_hw_params_set_rate_near(handle, params, &rrate, 0);
    if (err < 0) {
        errprint("Rate %uHz not available for playback: %s\n", AUDIO_RATE, snd_strerror(err));
        return err;
    }
    if (rrate != AUDIO_RATE) {
        errprint("Rate doesn't match (requested %uHz, get %iHz)\n", AUDIO_RATE, err);
        return -EINVAL;
    }
    /* set the buffer time */
    err = snd_pcm_hw_params_set_buffer_time_near(handle, params, &buffer_time, &dir);
    if (err < 0) {
        errprint("Unable to set buffer time %u for playback: %s\n", buffer_time, snd_strerror(err));
        return err;
    }
    err = snd_pcm_hw_params_get_buffer_size(params, &size);
    if (err < 0) {
        errprint("Unable to get buffer size for playback: %s\n", snd_strerror(err));
        return err;
    }
    buffer_size = size;
    /* set the period time */
    err = snd_pcm_hw_params_set_period_time_near(handle, params, &period_time, &dir);
    if (err < 0) {
        errprint("Unable to set period time %u for playback: %s\n", period_time, snd_strerror(err));
        return err;
    }
    err = snd_pcm_hw_params_get_period_size(params, &size, &dir);
    if (err < 0) {
        errprint("Unable to get period size for playback: %s\n", snd_strerror(err));
        return err;
    }
    period_size = size;
    /* write the parameters to device */
    err = snd_pcm_hw_params(handle, params);
    if (err < 0) {
        errprint("Unable to set hw params for playback: %s\n", snd_strerror(err));
        return err;
    }
    return 0;
}

static int set_swparams(snd_pcm_t *handle, snd_pcm_sw_params_t *swparams) {
    int err;
    /* get the current swparams */
    err = snd_pcm_sw_params_current(handle, swparams);
    if (err < 0) {
        errprint("Unable to determine current swparams for playback: %s\n", snd_strerror(err));
        return err;
    }

    /* start the transfer when the buffer is almost full: */
    /* (buffer_size / avail_min) * avail_min */
    err = snd_pcm_sw_params_set_start_threshold(handle, swparams, period_size);
    if (err < 0) {
        errprint("Unable to set start threshold mode for playback: %s\n", snd_strerror(err));
        return err;
    }

    /* allow the transfer when at least period_size samples can be processed */
    /* or disable this mechanism when period event is enabled (aka interrupt like style processing) */
    err = snd_pcm_sw_params_set_avail_min(handle, swparams, period_size);
    if (err < 0) {
        errprint("Unable to set avail min for playback: %s\n", snd_strerror(err));
        return err;
    }

    /* write the parameters to the playback device */
    err = snd_pcm_sw_params(handle, swparams);
    if (err < 0) {
        errprint("Unable to set sw params for playback: %s\n", snd_strerror(err));
        return err;
    }

    return 0;
}

static int xrun_recovery(snd_pcm_t *handle, int err) {
    dbgprint("stream recovery\n");

    if (err == -EPIPE) {    /* under-run */
        err = snd_pcm_prepare(handle);
        if (err < 0) errprint("Can't recover from underrun\n");
    }
    else if (err == -ESTRPIPE) {
        int limit = 60000;
        while ((err = snd_pcm_resume(handle)) == -EAGAIN) {
            usleep(1000);   /* wait until the suspend flag is released */
            if (--limit == 0) break;
        }

        if (err < 0) {
            err = snd_pcm_prepare(handle);
            if (err < 0) errprint("Can't recover from suspend\n");
        }
    }

    return err;
}

int snd_transfer_check(snd_pcm_t *handle, struct snd_transfer_s *transfer) {
    int err;

    snd_pcm_state_t state = snd_pcm_state(handle);
    if (state == SND_PCM_STATE_XRUN) {
        err = xrun_recovery(handle, -EPIPE);
        if (err < 0) {
            errprint("XRUN recovery failed: %s\n", snd_strerror(err));
            return err;
        }
        transfer->first = 1;
    } else if (state == SND_PCM_STATE_SUSPENDED) {
        err = xrun_recovery(handle, -ESTRPIPE);
        if (err < 0) {
            errprint("SUSPEND recovery failed: %s\n", snd_strerror(err));
            return err;
        }
    }

    snd_pcm_sframes_t avail = snd_pcm_avail_update(handle);
    if (avail < 0) {
        err = xrun_recovery(handle, avail);
        if (err < 0) {
            errprint("avail update failed: %s\n", snd_strerror(err));
            return err;
        }
        transfer->first = 1;
        return 0;
    }

    if (avail < period_size) {
        if (transfer->first) {
            transfer->first = 0;
            err = snd_pcm_start(handle);
            if (err < 0) {
                errprint("snd_pcm_start failed: %s\n", snd_strerror(err));
                return err;
            }
        } else {
            err = snd_pcm_wait(handle, 2000);
            if (err < 0) {
                if ((err = xrun_recovery(handle, err)) < 0) {
                    errprint("snd_pcm_wait error: %s\n", snd_strerror(err));
                    return err;
                }
                transfer->first = 1;
            }
        }
        return 0;
    }

    transfer->frames = period_size;
    err = snd_pcm_mmap_begin(handle, &transfer->my_areas, &transfer->offset, &transfer->frames);
    if (err < 0) {
        if ((err = xrun_recovery(handle, err)) < 0) {
            errprint("MMAP begin avail error: %s\n", snd_strerror(err));
            return err;
        }
        transfer->first = 1;
    }

    return 1;
}


int snd_transfer_commit(snd_pcm_t *handle, struct snd_transfer_s *transfer) {
    int err;
    snd_pcm_sframes_t commits = snd_pcm_mmap_commit(handle, transfer->offset, transfer->frames);
    // dbgprint("commited %ld/%ld frames\n", commits, transfer->frames);
    if (commits < 0 || (snd_pcm_uframes_t)commits != transfer->frames) {
        if ((err = xrun_recovery(handle, commits >= 0 ? -EPIPE : commits)) < 0) {
            errprint("MMAP commit error: %s\n", snd_strerror(err));
            return err;
        }
        transfer->first = 1;
    }

    return 0;
}


snd_pcm_t *find_snd_device(void) {
    int err, card, i;
    snd_pcm_t *handle = NULL;
    snd_pcm_hw_params_t *hwparams;
    snd_pcm_sw_params_t *swparams;

    snd_pcm_hw_params_alloca(&hwparams);
    snd_pcm_sw_params_alloca(&swparams);

    for (card = 0; card < 50; card++) {
        snprintf(snd_device, sizeof(snd_device), "/proc/asound/card%d/id", card);
        dbgprint("Trying %s\n", snd_device);

        FILE *fp = fopen(snd_device, "r");
        if (!fp)
            continue;

        err = fread(snd_device, 1, sizeof(snd_device), fp);
        fclose(fp);
        if (err <= 0 || strncmp(snd_device, "Loopback", 8) != 0)
            continue;

        for (i = 0; i < 8; i++) {
            snprintf(snd_device, sizeof(snd_device), "hw:%d,0,%d", card, i);
            dbgprint("Trying to open audio device: %s\n", snd_device);
            err = snd_pcm_open(&handle, snd_device, SND_PCM_STREAM_PLAYBACK, 0);
            if (err < 0 || !handle) {
                errprint("snd_pcm_open failed: %s\n", snd_strerror(err));
                continue;
            }

            // got a handle

            if (set_hwparams(handle, hwparams, SND_PCM_ACCESS_MMAP_INTERLEAVED) < 0) {
                errprint("setting audio hwparams failed\n");
                snd_pcm_close(handle);
                continue;
            }

            if (set_swparams(handle, swparams) < 0) {
                errprint("Setting audio swparams failed\n");
                snd_pcm_close(handle);
                goto OUT;
            }

            if (buffer_size != DROIDCAM_PCM_CHUNK_BYTES_2) {
                errprint("Unexpected audio device buffer size: %ld\n", buffer_size);
                snd_pcm_close(handle);
                goto OUT;
            }

            // update the buffer to have output device name, which will be shown in the UI
            snprintf(snd_device, sizeof(snd_device), "hw:%d,1,%d", card, i);
            return handle;
        }
    }

OUT:
    snd_device[0] = 0; // this will get shown on the UI, clear the value
    return NULL;
}
