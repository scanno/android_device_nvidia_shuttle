/**
 * Copyright (C) 2010 Eduardo Jos� Tagle <ejtagle@tutopia.com>
 *
 * Since deeply inspired from portaudio dev port:
 * Copyright (C) 2009-2010 r3gis (http://www.r3gis.fr)
 * Copyright (C) 2008-2009 Teluu Inc. (http://www.teluu.com)
 * Copyright (C) 2003-2008 Benny Prijono <benny@prijono.org>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */


#include <termios.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <semaphore.h>
#include <signal.h>
#include <linux/socket.h>
#include <sys/socket.h> 
#include <errno.h>
#include <stddef.h>
#include <sys/time.h>
#include <sys/select.h>

#include "audiochannel.h"

#define LOG_NDEBUG 0
#define LOG_TAG "RILAudioCh"
#include <utils/Log.h>

#include <system/audio.h>
#include <media/AudioRecord.h>
#include <media/AudioSystem.h>
#include <media/AudioTrack.h>

// ---- Android sound streaming ----

#define  AUDIOCHANNEL_DEBUG 0
#if AUDIOCHANNEL_DEBUG
#  define  D(...)   ALOGD(__VA_ARGS__)
#else
#  define  D(...)   ((void)0)
#endif 

/* Wait for pending output to be written on FD.  */
static int tcdrain (int fd)
{
	/* The TIOCSETP control waits for pending output to be written before
	affecting its changes, so we use that without changing anything.  */
	struct sgttyb b;
	if (ioctl (fd, TIOCGETP, (void *) &b) < 0 ||
		ioctl (fd, TIOCSETP, (void *) &b) < 0)
			return -1;
	return 0;
}

/* modemAudioIOThread:
    Output/inputs an audio frame (160 samples) to the 3G audio port of the cell modem
	We need to write to be able to read from modem
*/
static void* modemAudioIOThread(void* data)
{
	int            n;
    int            max_fd;
    fd_set         input;
    struct timeval timeout;

	struct GsmAudioTunnel* ctx = (struct GsmAudioTunnel*)data;
    int res = 0;

	ALOGD("modemAudioIOThread begin");
						
	// Discard all pending data*/
	ALOGD("Discarding old data....");
	tcflush(ctx->fd, TCIOFLUSH); 
	ALOGD("Discarding old data... Done");
						
	// Get audio from the queue and push it into the modem
	while (AudioQueue_isrunning(&ctx->rec_q) &&
		   AudioQueue_isrunning(&ctx->play_q)) {

		// Write audio to the 3G modem audio port in 320 bytes chunks... This is
		//  required by huawei modems...
		D("[T]Before AudioQueue_get");
		AudioQueue_get(&ctx->rec_q,ctx->play_buf,ctx->frame_size);
		D("[T]After AudioQueue_get");
		
		if (!AudioQueue_isrunning(&ctx->rec_q) || 
			!AudioQueue_isrunning(&ctx->play_q))
			break;

		// Write audio chunk
		D("[T]Before write");
		res = write(ctx->fd, ctx->play_buf, ctx->frame_size * (ctx->bits_per_sample/8));
		D("[T]After write: res: %d",res);
				  
		if (!AudioQueue_isrunning(&ctx->rec_q) || 
			!AudioQueue_isrunning(&ctx->play_q) ||
			 res < 0)
			break;

		// Make sure to drain previous audio to the modem - The grouping of writes does not work well with the voice channel
		tcdrain(ctx->fd);

		if (!AudioQueue_isrunning(&ctx->rec_q) || 
			!AudioQueue_isrunning(&ctx->play_q) ||
			 res < 0)
			break;
			
		// Read data from the modem
		D("[T]Before Select");

		do {
			// Initialize the input set
			FD_ZERO(&input);
			FD_SET(ctx->fd, &input);
			max_fd = ctx->fd + 1;

			// Initialize the timeout structure: 40ms is enough for this waiting
			timeout.tv_sec  = 0;
			timeout.tv_usec = 40000;

			// Do the select
			n = select(max_fd, &input, NULL, NULL, &timeout);

		} while (n == 0 &&
			AudioQueue_isrunning(&ctx->rec_q) &&
			AudioQueue_isrunning(&ctx->play_q));
		
		D("[T]After Select");		
		
		/* See if there was an error */
		if (!AudioQueue_isrunning(&ctx->rec_q) || 
			!AudioQueue_isrunning(&ctx->play_q) ||
			 n < 0)
			break;

		/* If something to read, read it */
		if (FD_ISSET(ctx->fd, &input)) {
			D("[T]Before read");
			res = read(ctx->fd, ctx->rec_buf, ctx->frame_size * (ctx->bits_per_sample/8));
			D("[T]After read: res: %d",res);
			
			if (!AudioQueue_isrunning(&ctx->play_q) || 
				!AudioQueue_isrunning(&ctx->rec_q) || 
				 res < 0)
				break;
			
		}


		// If muted, silence audio
		if (ctx->ismuted) {
			memset( ctx->rec_buf, 0, ctx->frame_size * (ctx->bits_per_sample/8));
		}
			
		// Write it to the audio queue
		D("[T]Before AudioQueue_add");
		AudioQueue_add(&ctx->play_q,ctx->rec_buf,ctx->frame_size);
		D("[T]After AudioQueue_add");
			
		
	};
	
	ALOGD("modemAudioIOThread ended");
    return NULL;
}

/* Called with audio sampled from mic */
static void AndroidRecorderCallback(int event, void* userData, void* info)
{
    struct GsmAudioTunnel *ctx = (struct GsmAudioTunnel*) userData;
    android::AudioRecord::Buffer* uinfo = (android::AudioRecord::Buffer*) info;
    unsigned nsamples;
    void *input;

    if(!ctx || !uinfo)
        return;

    if (!AudioQueue_isrunning(&ctx->rec_q))
        goto on_break;

    input = (void *) uinfo->raw;

    // Calculate number of total samples we've got
    nsamples = uinfo->frameCount;

	// Post data into the recording queue. Queue should self adapt and adjust sampling rate
	D("[A]Before AudioQueue_add");
	AudioQueue_add(&ctx->rec_q, input, nsamples);
	D("[A]After AudioQueue_add");
    return;

on_break:
	if (!ctx->rec_thread_exited) {
		ALOGD("Record thread stopped");
		ctx->rec_thread_exited = 1;
	}
    return;
}

/* Called to get audio samples to playback */
static void AndroidPlayerCallback( int event, void* userData, void* info)
{

    unsigned nsamples_req;
    void *output;
    struct GsmAudioTunnel *ctx = (struct GsmAudioTunnel*) userData;
    android::AudioTrack::Buffer* uinfo = (android::AudioTrack::Buffer*) info;

    if (!ctx || !uinfo)
        return;

    if (!AudioQueue_isrunning(&ctx->play_q))
        goto on_break;

    nsamples_req = uinfo->frameCount;
    output = (void*) uinfo->raw;

	// Read data from the Playback audioqueue
	D("[A]Before AudioQueue_get");
	AudioQueue_get(&ctx->play_q, output, nsamples_req);
	D("[A]After AudioQueue_get");
    return;

on_break:
	if (!ctx->play_thread_exited) {
		ALOGD("Play thread stopped");
		ctx->play_thread_exited = 1;
	}
	
	/* Silence output if we are not running */
	memset(output, 0, nsamples_req * (ctx->bits_per_sample >> 3));
    return;
}

 //AT^DDSETEX=2

int gsm_audio_tunnel_start(struct GsmAudioTunnel *ctx,const char* gsmvoicechannel,unsigned int sampling_rate,unsigned int frame_size,unsigned int bits_per_sample)
{
	pthread_attr_t modem_attr;
    struct termios newtio;
	int create_result = 0;
	size_t playBuffSize = 0;
	size_t playNotifyBuffSize = 0;
	size_t recBuffSize = 0;
	size_t recNotifyBuffSize = 0;

	audio_format_t format = (bits_per_sample > 8) 
        ? AUDIO_FORMAT_PCM_16_BIT
        : AUDIO_FORMAT_PCM_8_BIT;

    /* If already running, dont do it again */
    if (AudioQueue_isrunning(&ctx->rec_q) && 
		AudioQueue_isrunning(&ctx->play_q))
        return 0;

    memset(ctx,0,sizeof(struct GsmAudioTunnel));
	ctx->fd = -1;

    ctx->sampling_rate = sampling_rate;
    ctx->frame_size = frame_size;
    ctx->bits_per_sample = bits_per_sample;

	ALOGD("Opening GSM voice channel '%s', sampling_rate:%u hz, frame_size:%u, bits_per_sample:%u  ...",
        gsmvoicechannel,sampling_rate,frame_size,bits_per_sample);

	// Init the audioqueues
	if (AudioQueue_init(&ctx->play_q,15,bits_per_sample>>3) < 0) {
		ALOGE("Could not init Playback AudioQueue");
		goto error;
	}
	if (AudioQueue_init(&ctx->rec_q,15,bits_per_sample>>3) < 0) {
		ALOGE("Could not init Record AudioQueue");
		goto error;
	}
		
    // Open the device(com port) in blocking mode 
    ctx->fd = open(gsmvoicechannel, O_RDWR | O_NOCTTY);
    if (ctx->fd < 0) {
		ALOGE("Could not open '%s'",gsmvoicechannel);
		goto error;
    }
	 	
    // Configure it to get data as raw as possible
    tcgetattr(ctx->fd, &newtio );
    newtio.c_cflag = B115200 | CS8 | CLOCAL | CREAD;
    newtio.c_iflag = IGNPAR | IGNBRK | IGNCR | IXOFF;
    newtio.c_oflag = 0;
    newtio.c_lflag = 0;
    newtio.c_cc[VMIN]=1;
    newtio.c_cc[VTIME]=1;
    tcsetattr(ctx->fd,TCSANOW, &newtio);

	ALOGD("Creating streams....");
    ctx->rec_buf = malloc(ctx->frame_size * (ctx->bits_per_sample/8));
    if (!ctx->rec_buf) {
		ALOGE("Failed to allocate buffer for playback");
		goto error;
    }

    ctx->play_buf = malloc(ctx->frame_size * (ctx->bits_per_sample/8));
    if (!ctx->play_buf) {
		ALOGE("Failed to allocate buffer for record");
		goto error;
    }

    // Compute buffer sizes for record and playback
#if 0
    playBuffSize = 0;
    android::AudioSystem::getInputBufferSize(
                    ctx->sampling_rate, // Samples per second
                    format,
                    AUDIO_CHANNEL_IN_MONO,
                    &playBuffSize);
	recBuffSize = playBuffSize;
#else
	//android::AudioRecord::getMinFrameCount((int*)&recBuffSize,
	//                    ctx->sampling_rate, // Samples per second
	//					format,
	//					AUDIO_CHANNEL_IN_MONO);
						
    android::AudioSystem::getInputBufferSize(
                    ctx->sampling_rate, // Samples per second
                    format,
                    AUDIO_CHANNEL_IN_MONO,
                    &recBuffSize);
						
	android::AudioTrack::getMinFrameCount((int*)&playBuffSize,
						AUDIO_STREAM_VOICE_CALL,
	                    ctx->sampling_rate); // Samples per second
	recBuffSize	<<= 1; // Convert to bytes
	recBuffSize <<= 1; // Convert to bytes
	while (recBuffSize < frame_size)  recBuffSize <<= 1;
	while (playBuffSize < frame_size) playBuffSize <<= 1;
#endif

    // We use 2* size of input/output buffer for ping pong use of record/playback buffers.
    playNotifyBuffSize = playBuffSize;
	playBuffSize <<= 1;
	recNotifyBuffSize = recBuffSize;
    recBuffSize <<= 1;
	ALOGD("play bufsz: %d, record bufsz: %d",playNotifyBuffSize,recNotifyBuffSize);
	
    // Create audio record channel
    ctx->rec_strm = new android::AudioRecord();
    if(!ctx->rec_strm) {
		ALOGE("fail to create audio record");
		goto error;
    }

    // Unmute microphone
    // android::AudioSystem::muteMicrophone(false);
    create_result = ((android::AudioRecord*)ctx->rec_strm)->set(
                    AUDIO_SOURCE_MIC,
                    ctx->sampling_rate,
                    format,
                    AUDIO_CHANNEL_IN_MONO,
                    recBuffSize,
					android::AudioRecord::RECORD_AGC_ENABLE, 	//flags
                    &AndroidRecorderCallback,
                    (void *) ctx,
                    recNotifyBuffSize, // Notification frames
                    false,
                    0);

    if(create_result != android::NO_ERROR){
		ALOGE("fail to check audio record : error code %d", create_result);
		goto error;
    }

    if(((android::AudioRecord*)ctx->rec_strm)->initCheck() != android::NO_ERROR) {
		ALOGE("fail to check audio record : buffer size is : %d, error code : %d", recBuffSize, ((android::AudioRecord*)ctx->rec_strm)->initCheck() );
		goto error;
    }

    // Create audio playback channel
    ctx->play_strm = new android::AudioTrack();
    if(!ctx->play_strm) {
		ALOGE("Failed to create AudioTrack");
		goto error;
    }

    // android::AudioSystem::setMasterMute(false);
    create_result = ((android::AudioTrack*)ctx->play_strm)->set(
                    AUDIO_STREAM_VOICE_CALL,
                    ctx->sampling_rate, //this is sample rate in Hz (16000 Hz for example)
                    format,
                    AUDIO_CHANNEL_OUT_MONO, //For now this is mono (we expect 1)
                    playBuffSize,
					AUDIO_OUTPUT_FLAG_NONE, //flags
                    &AndroidPlayerCallback,
                    (void *) ctx,
                    playNotifyBuffSize,
                    0,
                    false,
                    0);

    if(create_result != android::NO_ERROR){
		ALOGE("fail to check audio record : error code %d", create_result);
		goto error;
    }

    if(((android::AudioTrack*)ctx->play_strm)->initCheck() != android::NO_ERROR) {
		ALOGE("fail to check audio playback : buffer size is : %d, error code : %d", playBuffSize, ((android::AudioTrack*)ctx->play_strm)->initCheck() );
		goto error;
    }

    /* Save the current audio routing setting, then switch it to earpiece. */
    // android::AudioSystem::getMode(&ctx->saved_audio_mode);
    // android::AudioSystem::getRouting(ctx->saved_audio_mode, &ctx->saved_audio_routing);
    // android::AudioSystem::setRouting(ctx->saved_audio_mode,
    //                      android::AudioSystem::ROUTE_EARPIECE,
    //                      android::AudioSystem::ROUTE_ALL);

	ALOGD("Starting streaming...");

    if (ctx->play_strm) {
        ((android::AudioTrack*)ctx->play_strm)->start();
    }

    if (ctx->rec_strm) {
        ((android::AudioRecord*)ctx->rec_strm)->start();
    }

	// Create the playback thread
	pthread_attr_init(&modem_attr);	
	if (pthread_create(&ctx->modem_t,&modem_attr,modemAudioIOThread,ctx) < 0) {
		ALOGE("Failed to start modemAudioIO Thread");
error:
		AudioQueue_end(&ctx->rec_q);
		AudioQueue_end(&ctx->play_q);
        if (ctx->play_strm) delete ((android::AudioTrack*)ctx->play_strm);
        if (ctx->rec_strm) delete ((android::AudioRecord*)ctx->rec_strm);
        if (ctx->play_buf) free(ctx->play_buf);
        if (ctx->rec_buf) free(ctx->rec_buf);
        if (ctx->fd) close(ctx->fd);
        return -1;
	}

	ALOGD("Done");

    // OK, done
    return 0;
}

/* API: mute audio record channel */
int gsm_audio_tunnel_mute(struct GsmAudioTunnel *ctx, int muteit)
{
	ctx->ismuted = muteit;
	return 0;
}

/* API: query if tunnel is running */
int gsm_audio_tunnel_running(struct GsmAudioTunnel *ctx)
{
	if (AudioQueue_isrunning(&ctx->rec_q) && 
		AudioQueue_isrunning(&ctx->play_q))
        return 1;
	return 0;
}

/* API: destroy ctx. */
int gsm_audio_tunnel_stop(struct GsmAudioTunnel *ctx)
{
    int i = 0;

    /* If not running, dont do it again */
    if (!AudioQueue_isrunning(&ctx->rec_q) || 
		!AudioQueue_isrunning(&ctx->play_q))
        return 0;

	ALOGD("Signal all audio threads to stop");
	AudioQueue_end(&ctx->play_q);
	AudioQueue_end(&ctx->rec_q);

	// Wait until android audio threads are "idling"
    for (i=0; 
		(!ctx->rec_thread_exited || !ctx->play_thread_exited) && i<100; 
		++i){
        usleep(100000);
	}
	 // After all sleep for 0.1 seconds since android device can be slow
    usleep(100000);
	ALOGD("Android audio threads are idle");

	if (ctx->rec_strm) { ((android::AudioRecord*)ctx->rec_strm)->stop(); }
    if (ctx->play_strm) { ((android::AudioTrack*)ctx->play_strm)->stop(); }
	ALOGD("Stopped android audio streaming");
	
	pthread_join(ctx->modem_t,NULL);
	ALOGD("End modemIO thread");

    // Restore the audio routing setting
    //      android::AudioSystem::setRouting(ctx->saved_audio_mode,
    //                      ctx->saved_audio_routing,
    //                      android::AudioSystem::ROUTE_ALL);


	ALOGD("Closing streaming");

	if (ctx->play_strm) delete ((android::AudioTrack*)ctx->play_strm);
	if (ctx->rec_strm) delete ((android::AudioRecord*)ctx->rec_strm);
	if (ctx->play_buf) free(ctx->play_buf);
	if (ctx->rec_buf) free(ctx->rec_buf);
	if (ctx->fd) close(ctx->fd);
	
    memset(ctx,0,sizeof(struct GsmAudioTunnel));

	ALOGD("Done");
    return 0;
}

