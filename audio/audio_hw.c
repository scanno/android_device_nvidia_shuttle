/*
 * Copyright (C) 2011 The Android Open Source Project
 * Copyright (C) 2011-12 Eduardo José Tagle <ejtagle@tutopia.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "audio_hw_primary"
#define LOG_NDEBUG 0

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/time.h>

#include <cutils/log.h>
#include <cutils/properties.h>
#include <cutils/str_parms.h>

#include <hardware/audio.h>
#include <hardware/hardware.h>

#include <system/audio.h>

#include <tinyalsa/asoundlib.h>

#include <audio_utils/resampler.h>
#include <audio_utils/echo_reference.h>
#include <hardware/audio_effect.h>
#include <audio_effects/effect_aec.h>

/* Mixer control names */
#define MIXER_PCM_PLAYBACK_VOLUME     		"PCM Playback Volume"
#define MIXER_HEADSET_PLAYBACK_VOLUME       "Headphone Playback Volume"
#define MIXER_SPEAKER_PLAYBACK_VOLUME       "Speaker Playback Volume"
#define MIXER_MIC_CAPTURE_VOLUME            "Mic 2 Capture Volume" /*ok*/

#define MIXER_HEADSET_PLAYBACK_SWITCH       "Headphone Playback Switch"
#define MIXER_SPEAKER_PLAYBACK_SWITCH       "Speaker Playback Switch"
#define MIXER_MIC_LEFT_CAPTURE_SWITCH       "Left Record Mixer Mic2L Capture Switch"
#define MIXER_MIC_RIGHT_CAPTURE_SWITCH      "Right Record Mixer Mic2R Capture Switch"

#define HEADPHONE_JACK_SWITCH				"Headphone Jack Switch"
#define INTERNAL_SPEAKER_SWITCH				"Internal Speaker Switch"
#define INTERNAL_MIC_SWITCH					"Internal Mic Switch"

/* ALSA card */
#define PCM_CARD_SHUTTLE 0

/* ALSA ports for card0 */
#define PCM_DEVICE_MM		0 /* CODEC port */
#define PCM_DEVICE_SCO 		1 /* Bluetooth/3G port */
#define PCM_DEVICE_SPDIF 	2 /* SPDIF (HDMI) port */

/* conversions from Percent to codec gains */
#define PERC_TO_PCM_VOLUME(x)     ( (int)((x) * 31 )) 
#define PERC_TO_CAPTURE_VOLUME(x) ( (int)((x) * 31 ))
#define PERC_TO_HEADSET_VOLUME(x) ( (int)((x) * 31 )) 
#define PERC_TO_SPEAKER_VOLUME(x) ( (int)((x) * 31 )) 

#define OUT_PERIOD_SIZE 880
#define OUT_SHORT_PERIOD_COUNT 2
#define OUT_LONG_PERIOD_COUNT 8
#define OUT_SAMPLING_RATE 44100

#define IN_PERIOD_SIZE 1024
#define IN_PERIOD_COUNT 4
#define IN_SAMPLING_RATE 44100

#define SCO_PERIOD_SIZE 256
#define SCO_PERIOD_COUNT 4
#define SCO_SAMPLING_RATE 8000

/* minimum sleep time in out_write() when write threshold is not reached */
#define MIN_WRITE_SLEEP_US 2000
#define MAX_WRITE_SLEEP_US ((OUT_PERIOD_SIZE * OUT_SHORT_PERIOD_COUNT * 1000000) \
                                / OUT_SAMPLING_RATE)

enum {
    OUT_BUFFER_TYPE_UNKNOWN,
    OUT_BUFFER_TYPE_SHORT,
    OUT_BUFFER_TYPE_LONG,
};

struct pcm_config pcm_config_out = {
    .channels = 2,
    .rate = OUT_SAMPLING_RATE,
    .period_size = OUT_PERIOD_SIZE,
    .period_count = OUT_LONG_PERIOD_COUNT,
    .format = PCM_FORMAT_S16_LE,
    .start_threshold = OUT_PERIOD_SIZE * OUT_SHORT_PERIOD_COUNT,
};

struct pcm_config pcm_config_in = {
    .channels = 2,
    .rate = IN_SAMPLING_RATE,
    .period_size = IN_PERIOD_SIZE,
    .period_count = IN_PERIOD_COUNT,
    .format = PCM_FORMAT_S16_LE,
    .start_threshold = 1,
    .stop_threshold = (IN_PERIOD_SIZE * IN_PERIOD_COUNT),
};

struct pcm_config pcm_config_sco = {
    .channels = 1,
    .rate = SCO_SAMPLING_RATE,
    .period_size = SCO_PERIOD_SIZE,
    .period_count = SCO_PERIOD_COUNT,
    .format = PCM_FORMAT_S16_LE,
};


struct route_setting
{
    char *ctl_name;
    int intval;
    char *strval;
};

/* These are values that never change */
struct route_setting defaults[] = {
    /* general */
    {
        .ctl_name = MIXER_PCM_PLAYBACK_VOLUME,
        .intval = PERC_TO_PCM_VOLUME(1),
    },
    {
        .ctl_name = MIXER_HEADSET_PLAYBACK_VOLUME,
        .intval = PERC_TO_HEADSET_VOLUME(1),
    },
    {
        .ctl_name = MIXER_SPEAKER_PLAYBACK_VOLUME,
        .intval = PERC_TO_SPEAKER_VOLUME(1),
    },
    {
        .ctl_name = MIXER_MIC_CAPTURE_VOLUME,
        .intval = PERC_TO_CAPTURE_VOLUME(1),
    },
    {
        .ctl_name = MIXER_HEADSET_PLAYBACK_SWITCH,
        .intval = 0,
    },
    {
        .ctl_name = HEADPHONE_JACK_SWITCH,
        .intval = 0,
    },
    {
        .ctl_name = MIXER_SPEAKER_PLAYBACK_SWITCH,
        .intval = 1,
    },
    {
        .ctl_name = INTERNAL_SPEAKER_SWITCH,
        .intval = 0,
    },
    {
        .ctl_name = MIXER_MIC_LEFT_CAPTURE_SWITCH,
        .intval = 1,
    },
    {
        .ctl_name = MIXER_MIC_RIGHT_CAPTURE_SWITCH,
        .intval = 1,
    },
    {
        .ctl_name = INTERNAL_MIC_SWITCH,
        .intval = 1,
    },
    {
        .ctl_name = NULL,
    }
};

/* Headphone playback route */
struct route_setting headphone_route[] = {
    {
        .ctl_name = HEADPHONE_JACK_SWITCH,
		.intval = 1,
    },
    {
        .ctl_name = MIXER_HEADSET_PLAYBACK_SWITCH,
		.intval = 1,
    },
    {
        .ctl_name = INTERNAL_SPEAKER_SWITCH,
		.intval = 0,
    },
    {
        .ctl_name = MIXER_SPEAKER_PLAYBACK_SWITCH,
		.intval = 0,
    },
    {
        .ctl_name = NULL,
    }
};

/* Speaker playback route */
struct route_setting speaker_route[] = {
    {
        .ctl_name = HEADPHONE_JACK_SWITCH,
		.intval = 0,
    },
    {
        .ctl_name = MIXER_HEADSET_PLAYBACK_SWITCH,
		.intval = 0,
    },
    {
        .ctl_name = INTERNAL_SPEAKER_SWITCH,
		.intval = 1,
    },
    {
        .ctl_name = MIXER_SPEAKER_PLAYBACK_SWITCH,
		.intval = 1,
    },
    {
        .ctl_name = NULL,
    }
};

/* Speaker Headphone playback route */
struct route_setting speaker_headphone_route[] = {
    {
        .ctl_name = HEADPHONE_JACK_SWITCH,
		.intval = 1,
    },
    {
        .ctl_name = MIXER_HEADSET_PLAYBACK_SWITCH,
		.intval = 1,
    },
    {
        .ctl_name = INTERNAL_SPEAKER_SWITCH,
		.intval = 1,
    },
    {
        .ctl_name = MIXER_SPEAKER_PLAYBACK_SWITCH,
		.intval = 1,
    },
    {
        .ctl_name = NULL,
    }
};

/* No out route */
struct route_setting no_out_route[] = {
    {
        .ctl_name = HEADPHONE_JACK_SWITCH,
		.intval = 0,
    },
    {
        .ctl_name = MIXER_HEADSET_PLAYBACK_SWITCH,
		.intval = 0,
    },
    {
        .ctl_name = INTERNAL_SPEAKER_SWITCH,
		.intval = 0,
    },
    {
        .ctl_name = MIXER_SPEAKER_PLAYBACK_SWITCH,
		.intval = 0,
    },
    {
        .ctl_name = NULL,
    }
};

struct mixer_ctls
{
	struct mixer_ctl *pcm_volume;
    struct mixer_ctl *headset_volume;
    struct mixer_ctl *speaker_volume;
	struct mixer_ctl *mic_volume;
	struct mixer_ctl *mic_switch_left;
	struct mixer_ctl *mic_switch_right;
};

/* The enable flag when 0 makes the assumption that enums are disabled by
 * "Off" and integers/booleans by 0 */
static int set_route_by_array(struct mixer *mixer, struct route_setting *route,
                              int enable)
{
    struct mixer_ctl *ctl;
    unsigned int i, j;

    /* Go through the route array and set each value */
    i = 0;
    while (route[i].ctl_name) {
        ctl = mixer_get_ctl_by_name(mixer, route[i].ctl_name);
        if (!ctl)
            return -EINVAL;

        if (route[i].strval) {
            if (enable)
                mixer_ctl_set_enum_by_string(ctl, route[i].strval);
            else
                mixer_ctl_set_enum_by_string(ctl, "Off");
        } else {
            /* This ensures multiple (i.e. stereo) values are set jointly */
            for (j = 0; j < mixer_ctl_get_num_values(ctl); j++) {
                if (enable)
                    mixer_ctl_set_value(ctl, j, route[i].intval);
                else
                    mixer_ctl_set_value(ctl, j, 0);
            }
        }
        i++;
    }

    return 0;
}


struct audio_device {
    struct audio_hw_device hw_device;

    pthread_mutex_t lock;       /* see note below on mutex acquisition order */
    struct mixer *mixer;
    struct mixer_ctls mixer_ctls;
    int mode;
    unsigned int devices;
	bool standby;
    bool mic_mute;
    struct echo_reference_itfe *echo_reference;

    int orientation;
    bool screen_off;

    struct stream_out *active_out;
    struct stream_in *active_in;
};

struct stream_out {
    struct audio_stream_out stream;

    pthread_mutex_t lock;       /* see note below on mutex acquisition order */
    struct pcm *pcm;
    struct pcm_config *pcm_config;
    bool standby;

    struct resampler_itfe *resampler;
    int16_t *buffer;
    size_t buffer_frames;

    struct echo_reference_itfe *echo_reference;
    
    int write_threshold;
    int cur_write_threshold;
    int buffer_type;

    struct audio_device *dev;
};

#define MAX_PREPROCESSORS 3 /* maximum one AGC + one NS + one AEC per input stream */

struct stream_in {
    struct audio_stream_in stream;

    pthread_mutex_t lock;       /* see note below on mutex acquisition order */
    struct pcm *pcm;
    struct pcm_config *pcm_config;
    bool standby;

	unsigned int requested_rate;
    struct resampler_itfe *resampler;
    struct resampler_buffer_provider buf_provider;
    int16_t *buffer;
    size_t buffer_size;
    size_t frames_in;

    struct echo_reference_itfe *echo_reference;
    bool need_echo_reference;
    effect_handle_t preprocessors[MAX_PREPROCESSORS];
    int num_preprocessors;
    int16_t *proc_buf;
    size_t proc_buf_size;
    size_t proc_frames_in;
    int16_t *ref_buf;
    size_t ref_buf_size;
    size_t ref_frames_in;

    int read_status;

    struct audio_device *dev;
};

enum {
    ORIENTATION_LANDSCAPE,
    ORIENTATION_PORTRAIT,
    ORIENTATION_SQUARE,
    ORIENTATION_UNDEFINED,
};

static uint32_t out_get_sample_rate(const struct audio_stream *stream);
static size_t out_get_buffer_size(const struct audio_stream *stream);
static audio_format_t out_get_format(const struct audio_stream *stream);
static uint32_t in_get_sample_rate(const struct audio_stream *stream);
static size_t in_get_buffer_size(const struct audio_stream *stream);
static audio_format_t in_get_format(const struct audio_stream *stream);
static int get_next_buffer(struct resampler_buffer_provider *buffer_provider,
                                   struct resampler_buffer* buffer);
static void release_buffer(struct resampler_buffer_provider *buffer_provider,
                                  struct resampler_buffer* buffer);

/*
 * NOTE: when multiple mutexes have to be acquired, always take the
 * audio_device mutex first, followed by the stream_in and/or
 * stream_out mutexes.
 */

/* Helper functions */

static void select_devices(struct audio_device *adev)
{
	/* Switch between speaker and headphone if required */
	switch (adev->devices & (AUDIO_DEVICE_OUT_SPEAKER | AUDIO_DEVICE_OUT_WIRED_HEADPHONE)) {
		case 0:
			set_route_by_array(adev->mixer, no_out_route, 1);
			break;
		case AUDIO_DEVICE_OUT_SPEAKER:
			set_route_by_array(adev->mixer, speaker_route, 1);
			break;
		case AUDIO_DEVICE_OUT_WIRED_HEADPHONE:
			set_route_by_array(adev->mixer, headphone_route, 1);
			break;
		case AUDIO_DEVICE_OUT_WIRED_HEADPHONE | AUDIO_DEVICE_OUT_SPEAKER:
			set_route_by_array(adev->mixer, speaker_headphone_route, 1);
			break;
	}

	ALOGD("Headphone out:%c, Speaker out:%c, HDMI out:%c, BT out:%c\n",
		(adev->devices & AUDIO_DEVICE_OUT_WIRED_HEADPHONE) ? 'Y' : 'N',
		(adev->devices & AUDIO_DEVICE_OUT_SPEAKER) ? 'Y' : 'N',
		(adev->devices & AUDIO_DEVICE_OUT_AUX_DIGITAL) ? 'Y' : 'N',
		(adev->devices & AUDIO_DEVICE_OUT_ALL_SCO) ? 'Y' : 'N'
		);

}

/* must be called with hw device and output stream mutexes locked */
static void do_out_standby(struct stream_out *out)
{
    struct audio_device *adev = out->dev;
	
    if (!out->standby) {
        pcm_close(out->pcm);
        out->pcm = NULL;
        adev->active_out = NULL;

        /* stop writing to echo reference */
        if (out->echo_reference) {
            out->echo_reference->write(out->echo_reference, NULL);
            out->echo_reference = NULL;
        }

        if (out->resampler) {
            release_resampler(out->resampler);
            out->resampler = NULL;
        }
        if (out->buffer) {
            free(out->buffer);
            out->buffer = NULL;
    }
        out->standby = true;
    }
}

static void put_echo_reference(struct audio_device *adev,
                          struct echo_reference_itfe *reference);


/* must be called with hw device and input stream mutexes locked */
static void do_in_standby(struct stream_in *in)
{
    struct audio_device *adev = in->dev;
	
    if (!in->standby) {
        pcm_close(in->pcm);
        in->pcm = NULL;
        adev->active_in = NULL;
	
        if (in->echo_reference != NULL) {
            /* stop reading from echo reference */
            in->echo_reference->read(in->echo_reference, NULL);
            put_echo_reference(adev, in->echo_reference);
            in->echo_reference = NULL;
        }
        if (in->resampler) {
            release_resampler(in->resampler);
            in->resampler = NULL;
        }
        if (in->buffer) {
            free(in->buffer);
            in->buffer = NULL;
        }
        in->standby = true;
    }
}

/* must be called with hw device and output stream mutexes locked */
static int start_output_stream(struct stream_out *out)
{
    struct audio_device *adev = out->dev;
    unsigned int device;
    int ret;

    /*
     * Due to the lack of sample rate converters in the SoC,
     * it greatly simplifies things to have only the main
     * (speaker/headphone) PCM or the BC SCO PCM open at
     * the same time.
     */
    if (adev->devices & AUDIO_DEVICE_OUT_ALL_SCO) {
        device = PCM_DEVICE_SCO;
        out->pcm_config = &pcm_config_sco;
    } else {
	    if (adev->devices & AUDIO_DEVICE_OUT_AUX_DIGITAL) {
        	device = PCM_DEVICE_SPDIF;
    	} else {
        	device = PCM_DEVICE_MM;
		}
        out->pcm_config = &pcm_config_out;
        out->buffer_type = OUT_BUFFER_TYPE_UNKNOWN;
    }

    /*
     * All open PCMs can only use a single group of rates at once:
     * Group 1: 11.025, 22.05, 44.1
     * Group 2: 8, 16, 32, 48
     * Group 1 is used for digital audio playback since 44.1 is
     * the most common rate, but group 2 is required for SCO.
     */
    if (adev->active_in) {
        pthread_mutex_lock(&adev->active_in->lock);
        if (((out->pcm_config->rate % 8000 == 0) &&
                 (adev->active_in->pcm_config->rate % 8000) != 0) ||
                 ((out->pcm_config->rate % 11025 == 0) &&
                 (adev->active_in->pcm_config->rate % 11025) != 0))
            do_in_standby(adev->active_in);
        pthread_mutex_unlock(&adev->active_in->lock);
    }
	
	ALOGD("start_output_stream: device:%d, rate:%d",device,out->pcm_config->rate);
    out->pcm = pcm_open(PCM_CARD_SHUTTLE, device, PCM_OUT | PCM_NORESTART, out->pcm_config);

    if (out->pcm && !pcm_is_ready(out->pcm)) {
        ALOGE("pcm_open(out) failed: %s", pcm_get_error(out->pcm));
        pcm_close(out->pcm);
        return -ENOMEM;
    }
	
    /*
     * If the stream rate differs from the PCM rate, we need to
     * create a resampler.
     */
    if (out_get_sample_rate(&out->stream.common) != out->pcm_config->rate) {
        ret = create_resampler(out_get_sample_rate(&out->stream.common),
                               out->pcm_config->rate,
                               out->pcm_config->channels,
                               RESAMPLER_QUALITY_DEFAULT,
                               NULL,
                               &out->resampler);
        out->buffer_frames = (pcm_config_out.period_size * out->pcm_config->rate) /
                out_get_sample_rate(&out->stream.common) + 1;

        out->buffer = malloc(pcm_frames_to_bytes(out->pcm, out->buffer_frames));
    }

    if (adev->echo_reference != NULL)
        out->echo_reference = adev->echo_reference;
	
	adev->active_out = out;

    return 0;
}

static struct echo_reference_itfe *get_echo_reference(struct audio_device *adev,
                                               audio_format_t format,
                                               uint32_t channel_count,
                                               uint32_t sampling_rate);

	
/* must be called with hw device and input stream mutexes locked */
static int start_input_stream(struct stream_in *in)
{
    struct audio_device *adev = in->dev;
    unsigned int device;
    int ret;

    /*
     * Due to the lack of sample rate converters in the SoC,
     * it greatly simplifies things to have only the main
     * mic PCM or the BC SCO PCM open at the same time.
     */
    if (adev->devices & AUDIO_DEVICE_IN_ALL_SCO) {
        device = PCM_DEVICE_SCO;
        in->pcm_config = &pcm_config_sco;
    } else {
	    if (adev->devices & AUDIO_DEVICE_IN_AUX_DIGITAL) {
        	device = PCM_DEVICE_SPDIF;
    	} else {
        	device = PCM_DEVICE_MM;
		}

        in->pcm_config = &pcm_config_in;
    }

    /*
     * All open PCMs can only use a single group of rates at once:
     * Group 1: 11.025, 22.05, 44.1
     * Group 2: 8, 16, 32, 48
     * Group 1 is used for digital audio playback since 44.1 is
     * the most common rate, but group 2 is required for SCO.
     */
    if (adev->active_out) {
        pthread_mutex_lock(&adev->active_out->lock);
        if (((in->pcm_config->rate % 8000 == 0) &&
                 (adev->active_out->pcm_config->rate % 8000) != 0) ||
                 ((in->pcm_config->rate % 11025 == 0) &&
                 (adev->active_out->pcm_config->rate % 11025) != 0))
            do_out_standby(adev->active_out);
        pthread_mutex_unlock(&adev->active_out->lock);
    }

    in->pcm = pcm_open(PCM_CARD_SHUTTLE, device, PCM_IN, in->pcm_config);

    if (in->pcm && !pcm_is_ready(in->pcm)) {
        ALOGE("pcm_open(in) failed: %s", pcm_get_error(in->pcm));
        pcm_close(in->pcm);
        return -ENOMEM;
    }

    if (in->need_echo_reference && in->echo_reference == NULL)
        in->echo_reference = get_echo_reference(adev,
                                        AUDIO_FORMAT_PCM_16_BIT,
                                        in->pcm_config->channels,
                                        in->requested_rate);

    /*
     * If the stream rate differs from the PCM rate, we need to
     * create a resampler.
     */
    if (in_get_sample_rate(&in->stream.common) != in->pcm_config->rate) {
        in->buf_provider.get_next_buffer = get_next_buffer;
        in->buf_provider.release_buffer = release_buffer;

        ret = create_resampler(in->pcm_config->rate,
                               in_get_sample_rate(&in->stream.common),
                               1,
                               RESAMPLER_QUALITY_DEFAULT,
                               &in->buf_provider,
                               &in->resampler);
    }
    in->buffer_size = pcm_frames_to_bytes(in->pcm,
                                          in->pcm_config->period_size);
    in->buffer = malloc(in->buffer_size);

	adev->active_in = in;

    return 0;
}

static int get_next_buffer(struct resampler_buffer_provider *buffer_provider,
                                   struct resampler_buffer* buffer)
{
    struct stream_in *in;
	
    if (buffer_provider == NULL || buffer == NULL)
        return -EINVAL;

    in = (struct stream_in *)((char *)buffer_provider -
                                   offsetof(struct stream_in, buf_provider));

    if (in->pcm == NULL) {
        buffer->raw = NULL;
        buffer->frame_count = 0;
        in->read_status = -ENODEV;
        return -ENODEV;
    }

    if (in->frames_in == 0) {
        in->read_status = pcm_read(in->pcm,
                                   (void*)in->buffer,
                                   in->buffer_size);
        if (in->read_status != 0) {
            ALOGE("get_next_buffer() pcm_read error %d", in->read_status);
            buffer->raw = NULL;
            buffer->frame_count = 0;
            return in->read_status;
        }
        in->frames_in = in->pcm_config->period_size;
        if (in->pcm_config->channels == 2) {
            unsigned int i;

            /* Discard right channel */
            for (i = 1; i < in->frames_in; i++)
                in->buffer[i] = in->buffer[i * 2];
        }
    }

    buffer->frame_count = (buffer->frame_count > in->frames_in) ?
                                in->frames_in : buffer->frame_count;
    buffer->i16 = in->buffer + (in->pcm_config->period_size - in->frames_in);

    return in->read_status;

}

static void release_buffer(struct resampler_buffer_provider *buffer_provider,
                                  struct resampler_buffer* buffer)
{
    struct stream_in *in;

    if (buffer_provider == NULL || buffer == NULL)
        return;
	
    in = (struct stream_in *)((char *)buffer_provider -
                                   offsetof(struct stream_in, buf_provider));

    in->frames_in -= buffer->frame_count;
}

/* read_frames() reads frames from kernel driver, down samples to capture rate
 * if necessary and output the number of frames requested to the buffer specified */
static ssize_t read_frames(struct stream_in *in, void *buffer, ssize_t frames)
{
    ssize_t frames_wr = 0;

    while (frames_wr < frames) {
        size_t frames_rd = frames - frames_wr;
        if (in->resampler != NULL) {
            in->resampler->resample_from_provider(in->resampler,
                    (int16_t *)((char *)buffer +
                            frames_wr * audio_stream_frame_size(&in->stream.common)),
                    &frames_rd);
        } else {
            struct resampler_buffer buf = {
                    { raw : NULL, },
                    frame_count : frames_rd,
            };
            get_next_buffer(&in->buf_provider, &buf);
            if (buf.raw != NULL) {
                memcpy((char *)buffer +
                           frames_wr * audio_stream_frame_size(&in->stream.common),
                        buf.raw,
                        buf.frame_count * audio_stream_frame_size(&in->stream.common));
                frames_rd = buf.frame_count;
            }
            release_buffer(&in->buf_provider, &buf);
        }
        /* in->read_status is updated by getNextBuffer() also called by
         * in->resampler->resample_from_provider() */
        if (in->read_status != 0)
            return in->read_status;

        frames_wr += frames_rd;
    }
    return frames_wr;
}

static void add_echo_reference(struct stream_out *out,
                               struct echo_reference_itfe *reference)
{
    pthread_mutex_lock(&out->lock);
    out->echo_reference = reference;
    pthread_mutex_unlock(&out->lock);
}

static void remove_echo_reference(struct stream_out *out,
                                  struct echo_reference_itfe *reference)
{
    pthread_mutex_lock(&out->lock);
    if (out->echo_reference == reference) {
        /* stop writing to echo reference */
        reference->write(reference, NULL);
        out->echo_reference = NULL;
    }
    pthread_mutex_unlock(&out->lock);
}

static void put_echo_reference(struct audio_device *adev,
                          struct echo_reference_itfe *reference)
{
    if (adev->echo_reference != NULL &&
            reference == adev->echo_reference) {
        if (adev->active_out != NULL)
            remove_echo_reference(adev->active_out, reference);
        release_echo_reference(reference);
        adev->echo_reference = NULL;
    }
}

static struct echo_reference_itfe *get_echo_reference(struct audio_device *adev,
                                               audio_format_t format,
                                               uint32_t channel_count,
                                               uint32_t sampling_rate)
{
    put_echo_reference(adev, adev->echo_reference);
    if (adev->active_out != NULL) {
        struct audio_stream *stream = &adev->active_out->stream.common;
        uint32_t wr_channel_count = popcount(stream->get_channels(stream));
        uint32_t wr_sampling_rate = stream->get_sample_rate(stream);

        int status = create_echo_reference(AUDIO_FORMAT_PCM_16_BIT,
                                           channel_count,
                                           sampling_rate,
                                           AUDIO_FORMAT_PCM_16_BIT,
                                           wr_channel_count,
                                           wr_sampling_rate,
                                           &adev->echo_reference);
        if (status == 0)
            add_echo_reference(adev->active_out, adev->echo_reference);
    }
    return adev->echo_reference;
}

static int get_playback_delay(struct stream_out *out,
                       size_t frames,
                       struct echo_reference_buffer *buffer)
{
    size_t kernel_frames;
    int status;

    status = pcm_get_htimestamp(out->pcm, &kernel_frames, &buffer->time_stamp);
    if (status < 0) {
        buffer->time_stamp.tv_sec  = 0;
        buffer->time_stamp.tv_nsec = 0;
        buffer->delay_ns           = 0;
        ALOGV("get_playback_delay(): pcm_get_htimestamp error,"
                "setting playbackTimestamp to 0");
        return status;
    }

    kernel_frames = pcm_get_buffer_size(out->pcm) - kernel_frames;

    /* adjust render time stamp with delay added by current driver buffer.
     * Add the duration of current frame as we want the render time of the last
     * sample being written. */
    buffer->delay_ns = (long)(((int64_t)(kernel_frames + frames)* 1000000000)/
                            out->pcm_config->rate);

    return 0;
}

/* xface */
static uint32_t out_get_sample_rate(const struct audio_stream *stream)
{
    return pcm_config_out.rate;
}

/* xface */
static int out_set_sample_rate(struct audio_stream *stream, uint32_t rate)
{
    return -ENOSYS;
}

/* xface */
static size_t out_get_buffer_size(const struct audio_stream *stream)
{
    return pcm_config_out.period_size *
               audio_stream_frame_size((struct audio_stream *)stream);
}

/* xface */
static uint32_t out_get_channels(const struct audio_stream *stream)
{
    return AUDIO_CHANNEL_OUT_STEREO;
}

/* xface */
static audio_format_t out_get_format(const struct audio_stream *stream)
{
    return AUDIO_FORMAT_PCM_16_BIT;
}

/* xface */
static int out_set_format(struct audio_stream *stream, audio_format_t format)
{
    return -ENOSYS;
}

/* xface */
static int out_standby(struct audio_stream *stream)
{
    struct stream_out *out = (struct stream_out *)stream;

    pthread_mutex_lock(&out->dev->lock);
    pthread_mutex_lock(&out->lock);
    do_out_standby(out);
    pthread_mutex_unlock(&out->lock);
    pthread_mutex_unlock(&out->dev->lock);

    return 0;
}

/* xface */
static int out_dump(const struct audio_stream *stream, int fd)
{
    return 0;
}

/* xface */
static int out_set_parameters(struct audio_stream *stream, const char *kvpairs)
{
    struct stream_out *out = (struct stream_out *)stream;
    struct audio_device *adev = out->dev;
    struct str_parms *parms;
    char value[32];
    int ret;
    unsigned int val;
	
    parms = str_parms_create_str(kvpairs);

    ret = str_parms_get_str(parms, AUDIO_PARAMETER_STREAM_ROUTING,
                            value, sizeof(value));
    pthread_mutex_lock(&adev->lock);
    if (ret >= 0) {
        val = atoi(value);
        if (((adev->devices & AUDIO_DEVICE_OUT_ALL) != val) && (val != 0)) {
            /*
             * If SCO is turned on/off or HDMI is turned on/off,
			 *  we need to put audio into standby
             *  because SCO uses a different PCM.
             */
            if ((val & AUDIO_DEVICE_OUT_ALL_SCO) ^
                    (adev->devices & AUDIO_DEVICE_OUT_ALL_SCO) ||
				(val & AUDIO_DEVICE_OUT_AUX_DIGITAL) ^
                    (adev->devices & AUDIO_DEVICE_OUT_AUX_DIGITAL)
				) {
			
                pthread_mutex_lock(&out->lock);
                do_out_standby(out);
                pthread_mutex_unlock(&out->lock);
			}
				
            adev->devices &= ~AUDIO_DEVICE_OUT_ALL;
            adev->devices |= val;
            select_devices(adev);
        }
        }
        pthread_mutex_unlock(&adev->lock);

    str_parms_destroy(parms);
    return ret;
}

/* xface */
static char * out_get_parameters(const struct audio_stream *stream, const char *keys)
{
    return strdup("");
}

/* xface */
static uint32_t out_get_latency(const struct audio_stream_out *stream)
{
    struct stream_out *out = (struct stream_out *)stream;
    struct audio_device *adev = out->dev;
    size_t period_count;

    pthread_mutex_lock(&adev->lock);

    if (adev->screen_off && !adev->active_in && !(adev->devices & AUDIO_DEVICE_OUT_ALL_SCO))
        period_count = OUT_LONG_PERIOD_COUNT;
    else
        period_count = OUT_SHORT_PERIOD_COUNT;

    pthread_mutex_unlock(&adev->lock);

    return (pcm_config_out.period_size * period_count * 1000) / pcm_config_out.rate;
}

/* xface */
static int out_set_volume(struct audio_stream_out *stream, float left,
                          float right)
{
    struct stream_out *out = (struct stream_out *)stream;
    struct audio_device *adev = out->dev;
	
	ALOGD("out_set_volume: left:%f, right:%f\n",left,right);

	mixer_ctl_set_value(adev->mixer_ctls.speaker_volume, 0,
		PERC_TO_SPEAKER_VOLUME(left));
	mixer_ctl_set_value(adev->mixer_ctls.speaker_volume, 1,
		PERC_TO_SPEAKER_VOLUME(right));
		
	mixer_ctl_set_value(adev->mixer_ctls.headset_volume, 0,
		PERC_TO_HEADSET_VOLUME(left));
	mixer_ctl_set_value(adev->mixer_ctls.headset_volume, 1,
		PERC_TO_HEADSET_VOLUME(right));

    return 0;
}

/* xface */
static ssize_t out_write(struct audio_stream_out *stream, const void* buffer,
                         size_t bytes)
{
    int ret = 0;
    struct stream_out *out = (struct stream_out *)stream;
    struct audio_device *adev = out->dev;
    size_t frame_size = audio_stream_frame_size(&out->stream.common);
    int16_t *in_buffer = (int16_t *)buffer;
    size_t in_frames = bytes / frame_size;
    size_t out_frames;
    int buffer_type;
    int kernel_frames;
    bool sco_on;
	
    /*
     * acquiring hw device mutex systematically is useful if a low
     * priority thread is waiting on the output stream mutex - e.g.
     * executing out_set_parameters() while holding the hw device
     * mutex
     */
    pthread_mutex_lock(&adev->lock);
    pthread_mutex_lock(&out->lock);
    if (out->standby) {
        ret = start_output_stream(out);
        if (ret != 0) {
            pthread_mutex_unlock(&adev->lock);
            goto exit;
        }
        out->standby = false;
    }
    buffer_type = (adev->screen_off && !adev->active_in) ?
            OUT_BUFFER_TYPE_LONG : OUT_BUFFER_TYPE_SHORT;
    sco_on = (adev->devices & AUDIO_DEVICE_OUT_ALL_SCO);
    pthread_mutex_unlock(&adev->lock);
	
    /* detect changes in screen ON/OFF state and adapt buffer size
     * if needed. Do not change buffer size when routed to SCO device. */
    if (!sco_on && (buffer_type != out->buffer_type)) {
        size_t period_count;

        if (buffer_type == OUT_BUFFER_TYPE_LONG)
            period_count = OUT_LONG_PERIOD_COUNT;
        else
            period_count = OUT_SHORT_PERIOD_COUNT;
	
        out->write_threshold = out->pcm_config->period_size * period_count;
        /* reset current threshold if exiting standby */
        if (out->buffer_type == OUT_BUFFER_TYPE_UNKNOWN)
            out->cur_write_threshold = out->write_threshold;
        out->buffer_type = buffer_type;
        }

    /* Reduce number of channels, if necessary */
    if (popcount(out_get_channels(&stream->common)) >
                 (int)out->pcm_config->channels) {
        unsigned int i;
	
        /* Discard right channel */
        for (i = 1; i < in_frames; i++)
            in_buffer[i] = in_buffer[i * 2];

        /* The frame size is now half */
        frame_size /= 2;
    }
	
    /* Change sample rate, if necessary */
    if (out_get_sample_rate(&stream->common) != out->pcm_config->rate) {
        out_frames = out->buffer_frames;
        out->resampler->resample_from_input(out->resampler,
                                            in_buffer, &in_frames,
                                            out->buffer, &out_frames);
        in_buffer = out->buffer;
    } else {
        out_frames = in_frames;
    }
    if (out->echo_reference != NULL) {
        struct echo_reference_buffer b;
        b.raw = (void *)buffer;
        b.frame_count = in_frames;

        get_playback_delay(out, out_frames, &b);
        out->echo_reference->write(out->echo_reference, &b);
    }

    if (!sco_on) {
        int total_sleep_time_us = 0;
        size_t period_size = out->pcm_config->period_size;

        /* do not allow more than out->cur_write_threshold frames in kernel
         * pcm driver buffer */
    do {
        struct timespec time_stamp;
            if (pcm_get_htimestamp(out->pcm,
                                   (unsigned int *)&kernel_frames,
                                   &time_stamp) < 0)
            break;
        kernel_frames = pcm_get_buffer_size(out->pcm) - kernel_frames;

            if (kernel_frames > out->cur_write_threshold) {
                int sleep_time_us =
                    (int)(((int64_t)(kernel_frames - out->cur_write_threshold)
                                    * 1000000) / out->pcm_config->rate);
                if (sleep_time_us < MIN_WRITE_SLEEP_US)
                    break;
                total_sleep_time_us += sleep_time_us;
                if (total_sleep_time_us > MAX_WRITE_SLEEP_US) {
                    ALOGW("out_write() limiting sleep time %d to %d",
                          total_sleep_time_us, MAX_WRITE_SLEEP_US);
                    sleep_time_us = MAX_WRITE_SLEEP_US -
                                        (total_sleep_time_us - sleep_time_us);
                }
                usleep(sleep_time_us);
            }

        } while ((kernel_frames > out->cur_write_threshold) &&
                (total_sleep_time_us <= MAX_WRITE_SLEEP_US));

        /* do not allow abrupt changes on buffer size. Increasing/decreasing
         * the threshold by steps of 1/4th of the buffer size keeps the write
         * time within a reasonable range during transitions.
         * Also reset current threshold just above current filling status when
         * kernel buffer is really depleted to allow for smooth catching up with
         * target threshold.
         */
        if (out->cur_write_threshold > out->write_threshold) {
            out->cur_write_threshold -= period_size / 4;
            if (out->cur_write_threshold < out->write_threshold) {
                out->cur_write_threshold = out->write_threshold;
            }
        } else if (out->cur_write_threshold < out->write_threshold) {
            out->cur_write_threshold += period_size / 4;
            if (out->cur_write_threshold > out->write_threshold) {
                out->cur_write_threshold = out->write_threshold;
            }
        } else if ((kernel_frames < out->write_threshold) &&
            ((out->write_threshold - kernel_frames) >
                (int)(period_size * OUT_SHORT_PERIOD_COUNT))) {
            out->cur_write_threshold = (kernel_frames / period_size + 1) * period_size;
            out->cur_write_threshold += period_size / 4;
        }
        }

    ret = pcm_write(out->pcm, in_buffer, out_frames * frame_size);
    if (ret == -EPIPE) {
        /* In case of underrun, don't sleep since we want to catch up asap */
        pthread_mutex_unlock(&out->lock);
        return ret;
    }

exit:
    pthread_mutex_unlock(&out->lock);

    if (ret != 0) {
        usleep(bytes * 1000000 / audio_stream_frame_size(&stream->common) /
               out_get_sample_rate(&stream->common));
    }

    return bytes;
}

/* xface */
static int out_get_render_position(const struct audio_stream_out *stream,
                                   uint32_t *dsp_frames)
{
    return -EINVAL;
}

/* xface */
static int out_add_audio_effect(const struct audio_stream *stream, effect_handle_t effect)
{
    return 0;
}

/* xface */
static int out_remove_audio_effect(const struct audio_stream *stream, effect_handle_t effect)
{
    return 0;
}

/* xface */
static int out_get_next_write_timestamp(const struct audio_stream_out *stream,
                                        int64_t *timestamp)
{
    return -EINVAL;
}

/** audio_stream_in implementation **/

/* xface */
static uint32_t in_get_sample_rate(const struct audio_stream *stream)
{
    struct stream_in *in = (struct stream_in *)stream;

    return in->requested_rate;
}

/* xface */
static int in_set_sample_rate(struct audio_stream *stream, uint32_t rate)
{
    return 0;
}

/* xface */
static size_t in_get_buffer_size(const struct audio_stream *stream)
{
    struct stream_in *in = (struct stream_in *)stream;
    size_t size;

    /*
     * take resampling into account and return the closest majoring
     * multiple of 16 frames, as audioflinger expects audio buffers to
     * be a multiple of 16 frames
     */
    size = (in->pcm_config->period_size * in_get_sample_rate(stream)) /
            in->pcm_config->rate;
    size = ((size + 15) / 16) * 16;

    return size * audio_stream_frame_size((struct audio_stream *)stream);
}

/* xface */
static uint32_t in_get_channels(const struct audio_stream *stream)
{
    struct stream_in *in = (struct stream_in *)stream;

    if (in->pcm_config->channels == 1) {
        return AUDIO_CHANNEL_IN_MONO;
    } else {
        return AUDIO_CHANNEL_IN_STEREO;
    }
}

/* xface */
static audio_format_t in_get_format(const struct audio_stream *stream)
{
    return AUDIO_FORMAT_PCM_16_BIT;
}

/* xface */
static int in_set_format(struct audio_stream *stream, audio_format_t format)
{
    return -ENOSYS;
}


/* xface */
static int in_standby(struct audio_stream *stream)
{
    struct stream_in *in = (struct stream_in *)stream;

    pthread_mutex_lock(&in->dev->lock);
    pthread_mutex_lock(&in->lock);
    do_in_standby(in);
    pthread_mutex_unlock(&in->lock);
    pthread_mutex_unlock(&in->dev->lock);

    return 0;
}

/* xface */
static int in_dump(const struct audio_stream *stream, int fd)
{
    return 0;
}

/* xface */
static int in_set_parameters(struct audio_stream *stream, const char *kvpairs)
{
    struct stream_in *in = (struct stream_in *)stream;
    struct audio_device *adev = in->dev;
    struct str_parms *parms;
    char value[32];
    int ret;
    unsigned int val;

    parms = str_parms_create_str(kvpairs);

    ret = str_parms_get_str(parms, AUDIO_PARAMETER_STREAM_ROUTING,
                            value, sizeof(value));
    pthread_mutex_lock(&adev->lock);
    if (ret >= 0) {
        val = atoi(value);
         if (((adev->devices & AUDIO_DEVICE_IN_ALL) != val) && (val != 0)) {
            /*
             * If SCO is turned on/off or HDMI is turned on/off, we need to put audio into standby
             * because SCO uses a different PCM.
             */
            if (((val & AUDIO_DEVICE_IN_ALL_SCO) ^
                    (adev->devices & AUDIO_DEVICE_IN_ALL_SCO)) || 
				((val & AUDIO_DEVICE_IN_AUX_DIGITAL) ^
                    (adev->devices & AUDIO_DEVICE_IN_AUX_DIGITAL))  
				) {
                pthread_mutex_lock(&in->lock);
                do_in_standby(in);
                pthread_mutex_unlock(&in->lock);
    }

            adev->devices &= ~AUDIO_DEVICE_IN_ALL;
            adev->devices |= val;
            select_devices(adev);
        }
    }
    pthread_mutex_unlock(&adev->lock);

    str_parms_destroy(parms);
    return ret;
}

/* xface */
static char * in_get_parameters(const struct audio_stream *stream,
                                const char *keys)
{
    return strdup("");
}

/* xface */
static int in_set_gain(struct audio_stream_in *stream, float gain)
{
    struct stream_in *in = (struct stream_in *)stream;
    struct audio_device *adev = in->dev;

	unsigned int channel;
	
    for (channel = 0; channel < 2; channel++) {
        mixer_ctl_set_value(adev->mixer_ctls.mic_volume, channel,
            PERC_TO_CAPTURE_VOLUME(gain));
        mixer_ctl_set_value(adev->mixer_ctls.mic_volume, channel,
            PERC_TO_CAPTURE_VOLUME(gain));
    }

    return 0;
}

static void get_capture_delay(struct stream_in *in,
                       size_t frames,
                       struct echo_reference_buffer *buffer)
{

    /* read frames available in kernel driver buffer */
    size_t kernel_frames;
    struct timespec tstamp;
    long buf_delay;
    long rsmp_delay;
    long kernel_delay;
    long delay_ns;

    if (pcm_get_htimestamp(in->pcm, &kernel_frames, &tstamp) < 0) {
        buffer->time_stamp.tv_sec  = 0;
        buffer->time_stamp.tv_nsec = 0;
        buffer->delay_ns           = 0;
        ALOGW("read get_capture_delay(): pcm_htimestamp error");
        return;
    }

    /* read frames available in audio HAL input buffer
     * add number of frames being read as we want the capture time of first sample
     * in current buffer */
    buf_delay = (long)(((int64_t)(in->frames_in + in->proc_frames_in) * 1000000000)
                                    / in->pcm_config->rate);
    /* add delay introduced by resampler */
    rsmp_delay = 0;
    if (in->resampler) {
        rsmp_delay = in->resampler->delay_ns(in->resampler);
    }

    kernel_delay = (long)(((int64_t)kernel_frames * 1000000000) / in->pcm_config->rate);

    delay_ns = kernel_delay + buf_delay + rsmp_delay;

    buffer->time_stamp = tstamp;
    buffer->delay_ns   = delay_ns;
    ALOGV("get_capture_delay time_stamp = [%ld].[%ld], delay_ns: [%d],"
         " kernel_delay:[%ld], buf_delay:[%ld], rsmp_delay:[%ld], kernel_frames:[%d], "
         "in->frames_in:[%d], in->proc_frames_in:[%d], frames:[%d]",
         buffer->time_stamp.tv_sec , buffer->time_stamp.tv_nsec, buffer->delay_ns,
         kernel_delay, buf_delay, rsmp_delay, kernel_frames,
         in->frames_in, in->proc_frames_in, frames);

}

static int32_t update_echo_reference(struct stream_in *in, size_t frames)
{
    struct echo_reference_buffer b;
    b.delay_ns = 0;

    ALOGV("update_echo_reference, frames = [%d], in->ref_frames_in = [%d],  "
          "b.frame_count = [%d]",
         frames, in->ref_frames_in, frames - in->ref_frames_in);
    if (in->ref_frames_in < frames) {
        if (in->ref_buf_size < frames) {
            in->ref_buf_size = frames;
            in->ref_buf = (int16_t *)realloc(in->ref_buf,
                                             in->ref_buf_size *
                                                 in->pcm_config->channels * sizeof(int16_t));
        }

        b.frame_count = frames - in->ref_frames_in;
        b.raw = (void *)(in->ref_buf + in->ref_frames_in * in->pcm_config->channels);

        get_capture_delay(in, frames, &b);

        if (in->echo_reference->read(in->echo_reference, &b) == 0)
        {
            in->ref_frames_in += b.frame_count;
            ALOGV("update_echo_reference: in->ref_frames_in:[%d], "
                    "in->ref_buf_size:[%d], frames:[%d], b.frame_count:[%d]",
                 in->ref_frames_in, in->ref_buf_size, frames, b.frame_count);
        }
    } else
        ALOGW("update_echo_reference: NOT enough frames to read ref buffer");
    return b.delay_ns;
}

static int set_preprocessor_param(effect_handle_t handle,
                           effect_param_t *param)
{
    uint32_t size = sizeof(int);
    uint32_t psize = ((param->psize - 1) / sizeof(int) + 1) * sizeof(int) +
                        param->vsize;

    int status = (*handle)->command(handle,
                                   EFFECT_CMD_SET_PARAM,
                                   sizeof (effect_param_t) + psize,
                                   param,
                                   &size,
                                   &param->status);
    if (status == 0)
        status = param->status;

    return status;
}

static int set_preprocessor_echo_delay(effect_handle_t handle,
                                     int32_t delay_us)
{
    uint32_t buf[sizeof(effect_param_t) / sizeof(uint32_t) + 2];
    effect_param_t *param = (effect_param_t *)buf;

    param->psize = sizeof(uint32_t);
    param->vsize = sizeof(uint32_t);
    *(uint32_t *)param->data = AEC_PARAM_ECHO_DELAY;
    *((int32_t *)param->data + 1) = delay_us;

    return set_preprocessor_param(handle, param);
}

static void push_echo_reference(struct stream_in *in, size_t frames)
{
    /* read frames from echo reference buffer and update echo delay
     * in->ref_frames_in is updated with frames available in in->ref_buf */
    int32_t delay_us = update_echo_reference(in, frames)/1000;
    int i;
    audio_buffer_t buf;

    if (in->ref_frames_in < frames)
        frames = in->ref_frames_in;

    buf.frameCount = frames;
    buf.raw = in->ref_buf;

    for (i = 0; i < in->num_preprocessors; i++) {
        if ((*in->preprocessors[i])->process_reverse == NULL)
            continue;

        (*in->preprocessors[i])->process_reverse(in->preprocessors[i],
                                               &buf,
                                               NULL);
        set_preprocessor_echo_delay(in->preprocessors[i], delay_us);
    }

    in->ref_frames_in -= buf.frameCount;
    if (in->ref_frames_in) {
        memcpy(in->ref_buf,
               in->ref_buf + buf.frameCount * in->pcm_config->channels,
               in->ref_frames_in * in->pcm_config->channels * sizeof(int16_t));
        }
}




/* process_frames() reads frames from kernel driver (via read_frames()),
 * calls the active audio pre processings and output the number of frames requested
 * to the buffer specified */
static ssize_t process_frames(struct stream_in *in, void* buffer, ssize_t frames)
{
    ssize_t frames_wr = 0;
    audio_buffer_t in_buf;
    audio_buffer_t out_buf;
    int i;

    while (frames_wr < frames) {
        /* first reload enough frames at the end of process input buffer */
        if (in->proc_frames_in < (size_t)frames) {
            ssize_t frames_rd;

            if (in->proc_buf_size < (size_t)frames) {
                in->proc_buf_size = (size_t)frames;
                in->proc_buf = (int16_t *)realloc(in->proc_buf,
                                         in->proc_buf_size *
                                             in->pcm_config->channels * sizeof(int16_t));
                ALOGV("process_frames(): in->proc_buf %p size extended to %d frames",
                     in->proc_buf, in->proc_buf_size);
            }
            frames_rd = read_frames(in,
                                    in->proc_buf +
                                        in->proc_frames_in * in->pcm_config->channels,
                                    frames - in->proc_frames_in);
            if (frames_rd < 0) {
                frames_wr = frames_rd;
                break;
            }
            in->proc_frames_in += frames_rd;
        }

        if (in->echo_reference != NULL)
            push_echo_reference(in, in->proc_frames_in);

         /* in_buf.frameCount and out_buf.frameCount indicate respectively
          * the maximum number of frames to be consumed and produced by process() */
        in_buf.frameCount = in->proc_frames_in;
        in_buf.s16 = in->proc_buf;
        out_buf.frameCount = frames - frames_wr;
        out_buf.s16 = (int16_t *)buffer + frames_wr * in->pcm_config->channels;

        for (i = 0; i < in->num_preprocessors; i++)
            (*in->preprocessors[i])->process(in->preprocessors[i],
                                               &in_buf,
                                               &out_buf);

        /* process() has updated the number of frames consumed and produced in
         * in_buf.frameCount and out_buf.frameCount respectively
         * move remaining frames to the beginning of in->proc_buf */
        in->proc_frames_in -= in_buf.frameCount;
        if (in->proc_frames_in) {
            memcpy(in->proc_buf,
                   in->proc_buf + in_buf.frameCount * in->pcm_config->channels,
                   in->proc_frames_in * in->pcm_config->channels * sizeof(int16_t));
        }

        /* if not enough frames were passed to process(), read more and retry. */
        if (out_buf.frameCount == 0)
            continue;

        frames_wr += out_buf.frameCount;
    }
    return frames_wr;
}

/* xface */
static ssize_t in_read(struct audio_stream_in *stream, void* buffer,
                       size_t bytes)
{
    int ret = 0;
    struct stream_in *in = (struct stream_in *)stream;
    struct audio_device *adev = in->dev;
    size_t frames_rq = bytes / audio_stream_frame_size(&stream->common);

    /*
     * acquiring hw device mutex systematically is useful if a low
     * priority thread is waiting on the input stream mutex - e.g.
     * executing in_set_parameters() while holding the hw device
     * mutex
     */
    pthread_mutex_lock(&adev->lock);
    pthread_mutex_lock(&in->lock);
    if (in->standby) {
        ret = start_input_stream(in);
        if (ret == 0)
            in->standby = 0;
    }
    pthread_mutex_unlock(&adev->lock);

    if (ret < 0)
        goto exit;

    if (in->num_preprocessors != 0)
        ret = process_frames(in, buffer, frames_rq);
    else if (in->resampler != NULL)
        ret = read_frames(in, buffer, frames_rq);
    else if (in->pcm_config->channels == 2) {
        /*
         * If the PCM is stereo, capture twice as many frames and
         * discard the right channel.
         */
        unsigned int i;
        int16_t *in_buffer = (int16_t *)buffer;

        ret = pcm_read(in->pcm, in->buffer, bytes * 2);

        /* Discard right channel */
        for (i = 0; i < frames_rq; i++)
            in_buffer[i] = in->buffer[i * 2];
    } else {
        ret = pcm_read(in->pcm, buffer, bytes);
    }

    if (ret > 0)
        ret = 0;

    /*
     * Instead of writing zeroes here, we could trust the hardware
     * to always provide zeroes when muted.
     */
    if (ret == 0 && adev->mic_mute)
        memset(buffer, 0, bytes);

exit:
    if (ret < 0)
        usleep(bytes * 1000000 / audio_stream_frame_size(&stream->common) /
               in_get_sample_rate(&stream->common));

    pthread_mutex_unlock(&in->lock);
    return bytes;
}

/* xface */
static uint32_t in_get_input_frames_lost(struct audio_stream_in *stream)
{
    return 0;
}

/* xface */
static int in_add_audio_effect(const struct audio_stream *stream,
                               effect_handle_t effect)
{
    struct stream_in *in = (struct stream_in *)stream;
    int status;
    effect_descriptor_t desc;

    pthread_mutex_lock(&in->dev->lock);
    pthread_mutex_lock(&in->lock);
    if (in->num_preprocessors >= MAX_PREPROCESSORS) {
        status = -ENOSYS;
        goto exit;
    }

    status = (*effect)->get_descriptor(effect, &desc);
    if (status != 0)
        goto exit;

    in->preprocessors[in->num_preprocessors++] = effect;

    if (memcmp(&desc.type, FX_IID_AEC, sizeof(effect_uuid_t)) == 0) {
        in->need_echo_reference = true;
        do_in_standby(in);
    }

exit:

    pthread_mutex_unlock(&in->lock);
    pthread_mutex_unlock(&in->dev->lock);
    return status;
}

/* xface */
static int in_remove_audio_effect(const struct audio_stream *stream,
                                  effect_handle_t effect)
{
    struct stream_in *in = (struct stream_in *)stream;
    int i;
    int status = -EINVAL;
    bool found = false;
    effect_descriptor_t desc;

    pthread_mutex_lock(&in->dev->lock);
    pthread_mutex_lock(&in->lock);
    if (in->num_preprocessors <= 0) {
        status = -ENOSYS;
        goto exit;
    }

    for (i = 0; i < in->num_preprocessors; i++) {
        if (found) {
            in->preprocessors[i - 1] = in->preprocessors[i];
            continue;
        }
        if (in->preprocessors[i] == effect) {
            in->preprocessors[i] = NULL;
            status = 0;
            found = true;
        }
    }

    if (status != 0)
        goto exit;

    in->num_preprocessors--;

    status = (*effect)->get_descriptor(effect, &desc);
    if (status != 0)
        goto exit;
    if (memcmp(&desc.type, FX_IID_AEC, sizeof(effect_uuid_t)) == 0) {
        in->need_echo_reference = false;
        do_in_standby(in);
    }

exit:

    pthread_mutex_unlock(&in->lock);
    pthread_mutex_unlock(&in->dev->lock);
    return status;
}

/* xface */
static int adev_open_output_stream(struct audio_hw_device *dev,
                                   audio_io_handle_t handle,
                                   audio_devices_t devices,
                                   audio_output_flags_t flags,
                                   struct audio_config *config,
                                   struct audio_stream_out **stream_out)
{
    struct audio_device *adev = (struct audio_device *)dev;
    struct stream_out *out;
    int ret;

	ALOGD("adev_open_output_stream");
	
    out = (struct stream_out *)calloc(1, sizeof(struct stream_out));
    if (!out)
        return -ENOMEM;

    out->stream.common.get_sample_rate = out_get_sample_rate;
    out->stream.common.set_sample_rate = out_set_sample_rate;
    out->stream.common.get_buffer_size = out_get_buffer_size;
    out->stream.common.get_channels = out_get_channels;
    out->stream.common.get_format = out_get_format;
    out->stream.common.set_format = out_set_format;
    out->stream.common.standby = out_standby;
    out->stream.common.dump = out_dump;
    out->stream.common.set_parameters = out_set_parameters;
    out->stream.common.get_parameters = out_get_parameters;
    out->stream.common.add_audio_effect = out_add_audio_effect;
    out->stream.common.remove_audio_effect = out_remove_audio_effect;
    out->stream.get_latency = out_get_latency;
    out->stream.set_volume = out_set_volume;
    out->stream.write = out_write;
    out->stream.get_render_position = out_get_render_position;
    out->stream.get_next_write_timestamp = out_get_next_write_timestamp;

    out->dev = adev;

    config->format = out_get_format(&out->stream.common);
    config->channel_mask = out_get_channels(&out->stream.common);
    config->sample_rate = out_get_sample_rate(&out->stream.common);

    out->standby = true;

    *stream_out = &out->stream;
    return 0;

err_open:
    free(out);
    *stream_out = NULL;
    return ret;
}

/* xface */
static void adev_close_output_stream(struct audio_hw_device *dev,
                                     struct audio_stream_out *stream)
{
	ALOGD("adev_close_output_stream");
	
    out_standby(&stream->common);
    free(stream);
}

/* xface */
static int adev_set_parameters(struct audio_hw_device *dev, const char *kvpairs)
{
    struct audio_device *adev = (struct audio_device *)dev;
    struct str_parms *parms;
	char *str;
    char value[32];
    int ret;
   
	ALOGD("adev_set_parameters: kppairs: %s", kvpairs);
    parms = str_parms_create_str(kvpairs);
    ret = str_parms_get_str(parms, "orientation", value, sizeof(value));
    if (ret >= 0) {
        int orientation;

        if (strcmp(value, "landscape") == 0)
            orientation = ORIENTATION_LANDSCAPE;
        else if (strcmp(value, "portrait") == 0)
            orientation = ORIENTATION_PORTRAIT;
        else if (strcmp(value, "square") == 0)
            orientation = ORIENTATION_SQUARE;
        else
            orientation = ORIENTATION_UNDEFINED;

        pthread_mutex_lock(&adev->lock);
        if (orientation != adev->orientation) {
            adev->orientation = orientation;
            /*
             * Orientation changes can occur with the input device
             * closed so we must call select_devices() here to set
             * up the mixer. This is because select_devices() will
             * not be called when the input device is opened if no
             * other input parameter is changed.
             */
            select_devices(adev);
        }
        pthread_mutex_unlock(&adev->lock);
    }
	
	/* Get the screen state as system power indicator */
	ret = str_parms_get_str(parms, "screen_state", value, sizeof(value));
    if (ret >= 0) {
        if (strcmp(value, AUDIO_PARAMETER_VALUE_ON) == 0)
            adev->screen_off = false;
        else
            adev->screen_off = true;
    }

    str_parms_destroy(parms);
    return ret;
}

/* xface */
static char * adev_get_parameters(const struct audio_hw_device *dev,
                                  const char *keys)
{
    return strdup("");
}

/* xface */
static int adev_init_check(const struct audio_hw_device *dev)
{
    return 0;
}

/* xface */
static int adev_set_voice_volume(struct audio_hw_device *dev, float volume)
{
	ALOGD("adev_set_voice_volume: volume: %f", volume);
	
    return -ENOSYS;
}

/* xface */
static int adev_set_master_volume(struct audio_hw_device *dev, float volume)
{
	struct audio_device *adev = (struct audio_device *)dev;

	ALOGD("adev_set_master_volume: volume: %f", volume);
	
	mixer_ctl_set_value(adev->mixer_ctls.pcm_volume, 0,
		PERC_TO_PCM_VOLUME(volume));
	mixer_ctl_set_value(adev->mixer_ctls.pcm_volume, 1,
		PERC_TO_PCM_VOLUME(volume));

    return 0;
}

/* xface */
static int adev_set_mode(struct audio_hw_device *dev, int mode)
{
	ALOGD("adev_set_mode: mode: %d", mode);
    return 0;
}

/* xface */
static int adev_set_mic_mute(struct audio_hw_device *dev, bool state)
{
    struct audio_device *adev = (struct audio_device *)dev;

	ALOGD("adev_set_mic_mute: state: %d", state);
	
    adev->mic_mute = state;

	/* Disable mic if requested */
	mixer_ctl_set_value(adev->mixer_ctls.mic_switch_left, 0,	state ? 0 : 1);
	mixer_ctl_set_value(adev->mixer_ctls.mic_switch_right, 0,	state ? 0 : 1);
	
    return 0;
}

/* xface */
static int adev_get_mic_mute(const struct audio_hw_device *dev, bool *state)
{
    struct audio_device *adev = (struct audio_device *)dev;

    *state = adev->mic_mute;

    return 0;
}

/* xface */
static size_t adev_get_input_buffer_size(const struct audio_hw_device *dev,
                                         const struct audio_config *config)
{
    size_t size;
	ALOGD("adev_get_input_buffer_size: sample_rate: %d, format: %d, channel_count:%d", config->sample_rate, config->format, popcount(config->channel_mask));
	
    /*
     * take resampling into account and return the closest majoring
     * multiple of 16 frames, as audioflinger expects audio buffers to
     * be a multiple of 16 frames
     */
    size = (pcm_config_in.period_size * config->sample_rate) / pcm_config_in.rate;
    size = ((size + 15) / 16) * 16;

    return (size * popcount(config->channel_mask) *
                audio_bytes_per_sample(config->format));
}

/* xface */
static int adev_open_input_stream(struct audio_hw_device *dev,
                                  audio_io_handle_t handle,
                                  audio_devices_t devices,
                                  struct audio_config *config,
                                  struct audio_stream_in **stream_in)
{
    struct audio_device *adev = (struct audio_device *)dev;
    struct stream_in *in;
    int ret;

	ALOGD("adev_open_input_stream: channel_count:%d", popcount(config->channel_mask));
	
	*stream_in = NULL;

    /* Respond with a request for mono if a different format is given. */
    if (config->channel_mask != AUDIO_CHANNEL_IN_MONO) {
        config->channel_mask = AUDIO_CHANNEL_IN_MONO;
        return -EINVAL;
    }

    in = (struct stream_in *)calloc(1, sizeof(struct stream_in));
    if (!in)
        return -ENOMEM;

    in->stream.common.get_sample_rate = in_get_sample_rate;
    in->stream.common.set_sample_rate = in_set_sample_rate;
    in->stream.common.get_buffer_size = in_get_buffer_size;
    in->stream.common.get_channels = in_get_channels;
    in->stream.common.get_format = in_get_format;
    in->stream.common.set_format = in_set_format;
    in->stream.common.standby = in_standby;
    in->stream.common.dump = in_dump;
    in->stream.common.set_parameters = in_set_parameters;
    in->stream.common.get_parameters = in_get_parameters;
    in->stream.common.add_audio_effect = in_add_audio_effect;
    in->stream.common.remove_audio_effect = in_remove_audio_effect;
    in->stream.set_gain = in_set_gain;
    in->stream.read = in_read;
    in->stream.get_input_frames_lost = in_get_input_frames_lost;

    in->dev = adev;
    in->standby = true;
    in->requested_rate = config->sample_rate;
    in->pcm_config = &pcm_config_in; /* default PCM config */

    *stream_in = &in->stream;
    return 0;
}

/* xface */
static void adev_close_input_stream(struct audio_hw_device *dev,
                                   struct audio_stream_in *stream)
{
    struct stream_in *in = (struct stream_in *)stream;

	ALOGD("adev_close_input_stream");
	
    in_standby(&stream->common);
    free(stream);
}

/* xface */
static int adev_dump(const audio_hw_device_t *device, int fd)
{
    return 0;
}

/* xface */
static int adev_close(hw_device_t *device)
{
    struct audio_device *adev = (struct audio_device *)device;
	
	ALOGD("adev_close");

    mixer_close(adev->mixer);
    free(device);
    return 0;
}

/* xface */
static uint32_t adev_get_supported_devices(const struct audio_hw_device *dev)
{
	ALOGD("adev_get_supported_devices");
    return (/* OUT */
            AUDIO_DEVICE_OUT_SPEAKER |
            AUDIO_DEVICE_OUT_WIRED_HEADPHONE |
            AUDIO_DEVICE_OUT_AUX_DIGITAL |
			AUDIO_DEVICE_OUT_ALL_SCO |
            AUDIO_DEVICE_OUT_DEFAULT |
            /* IN */
            AUDIO_DEVICE_IN_BUILTIN_MIC |
			AUDIO_DEVICE_IN_ALL_SCO |
            AUDIO_DEVICE_IN_DEFAULT);
}

static int adev_open(const hw_module_t* module, const char* name,
                     hw_device_t** device)
{
    struct audio_device *adev;
    int ret;

	ALOGD("adev_open: name:'%s'",name);
	
    if (strcmp(name, AUDIO_HARDWARE_INTERFACE) != 0)
        return -EINVAL;

    adev = calloc(1, sizeof(struct audio_device));
    if (!adev)
        return -ENOMEM;

    adev->hw_device.common.tag = HARDWARE_DEVICE_TAG;
    adev->hw_device.common.version = AUDIO_DEVICE_API_VERSION_1_0;
    adev->hw_device.common.module = (struct hw_module_t *) module;
    adev->hw_device.common.close = adev_close;

    adev->hw_device.get_supported_devices = adev_get_supported_devices;
    adev->hw_device.init_check = adev_init_check;
    adev->hw_device.set_voice_volume = adev_set_voice_volume;
    adev->hw_device.set_master_volume = adev_set_master_volume;
    adev->hw_device.set_mode = adev_set_mode;
    adev->hw_device.set_mic_mute = adev_set_mic_mute;
    adev->hw_device.get_mic_mute = adev_get_mic_mute;
    adev->hw_device.set_parameters = adev_set_parameters;
    adev->hw_device.get_parameters = adev_get_parameters;
    adev->hw_device.get_input_buffer_size = adev_get_input_buffer_size;
    adev->hw_device.open_output_stream = adev_open_output_stream;
    adev->hw_device.close_output_stream = adev_close_output_stream;
    adev->hw_device.open_input_stream = adev_open_input_stream;
    adev->hw_device.close_input_stream = adev_close_input_stream;
    adev->hw_device.dump = adev_dump;

    adev->mixer = mixer_open(0);
    if (!adev->mixer) {
        free(adev);
        ALOGE("Unable to open the mixer, aborting.");
        return -EINVAL;
    }

	adev->orientation = ORIENTATION_UNDEFINED;
    adev->mixer_ctls.mic_volume = mixer_get_ctl_by_name(adev->mixer,
                                           MIXER_MIC_CAPTURE_VOLUME);
	if (!adev->mixer_ctls.mic_volume) { 
		ALOGE("Unable to find '%s' mixer control",MIXER_MIC_CAPTURE_VOLUME);
		goto error_out;
	}
	
    adev->mixer_ctls.pcm_volume = mixer_get_ctl_by_name(adev->mixer,
                                           MIXER_PCM_PLAYBACK_VOLUME);
	if (!adev->mixer_ctls.pcm_volume) { 
		ALOGE("Unable to find '%s' mixer control",MIXER_PCM_PLAYBACK_VOLUME);
		goto error_out;
	}
										   
    adev->mixer_ctls.headset_volume = mixer_get_ctl_by_name(adev->mixer,
                                           MIXER_HEADSET_PLAYBACK_VOLUME);
	if (!adev->mixer_ctls.headset_volume) { 
		ALOGE("Unable to find '%s' mixer control",MIXER_HEADSET_PLAYBACK_VOLUME);
		goto error_out;
	}
										   
    adev->mixer_ctls.speaker_volume = mixer_get_ctl_by_name(adev->mixer,
                                           MIXER_SPEAKER_PLAYBACK_VOLUME);
	if (!adev->mixer_ctls.speaker_volume) { 
		ALOGE("Unable to find '%s' mixer control",MIXER_SPEAKER_PLAYBACK_VOLUME);
		goto error_out;
	}

    adev->mixer_ctls.mic_switch_left = mixer_get_ctl_by_name(adev->mixer,
                                           MIXER_MIC_LEFT_CAPTURE_SWITCH);
	if (!adev->mixer_ctls.mic_switch_left) { 
		ALOGE("Unable to find '%s' mixer control",MIXER_MIC_LEFT_CAPTURE_SWITCH);
		goto error_out;
	}

    adev->mixer_ctls.mic_switch_right = mixer_get_ctl_by_name(adev->mixer,
                                           MIXER_MIC_RIGHT_CAPTURE_SWITCH);
	if (!adev->mixer_ctls.mic_switch_right) { 
		ALOGE("Unable to find '%s' mixer control",MIXER_MIC_RIGHT_CAPTURE_SWITCH);
		goto error_out;
	}

	
    /* Set the default route before the PCM stream is opened */
    pthread_mutex_lock(&adev->lock);
    set_route_by_array(adev->mixer, defaults, 1);
    adev->devices = AUDIO_DEVICE_OUT_SPEAKER | AUDIO_DEVICE_IN_BUILTIN_MIC;
    pthread_mutex_unlock(&adev->lock);

    *device = &adev->hw_device.common;

    return 0;

error_out:	

#if !LOG_NDEBUG
	/* To aid debugging, dump all mixer controls */
	{
		unsigned int cnt = mixer_get_num_ctls(adev->mixer);
		unsigned int i;
		ALOGD("Mixer dump: Nr of controls: %d",cnt);
		for (i = 0; i < cnt; i++) {
			struct mixer_ctl* x = mixer_get_ctl(adev->mixer,i);
			if (x != NULL) {
				const char* name;
				const char* type;
				name = mixer_ctl_get_name(x);
				type = mixer_ctl_get_type_string(x);
				ALOGD("#%d: '%s' [%s]",i,name,type);		
			}
		}
	}
#endif

    mixer_close(adev->mixer);
    free(adev);
    return -EINVAL;
	
}

static struct hw_module_methods_t hal_module_methods = {
    .open = adev_open,
};

struct audio_module HAL_MODULE_INFO_SYM = {
    .common = {
        .tag = HARDWARE_MODULE_TAG,
        .module_api_version = AUDIO_MODULE_API_VERSION_0_1,
        .hal_api_version = HARDWARE_HAL_API_VERSION,
        .id = AUDIO_HARDWARE_MODULE_ID,
        .name = "Shuttle audio HW HAL",
        .author = "The Android Open Source Project",
        .methods = &hal_module_methods,
    },
};
