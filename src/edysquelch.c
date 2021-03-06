
#if HAVE_CONFIG_H
#include "../config.h"
#endif

#include <stdio.h>
#include <stdlib.h>

#include "edysquelch.h"
#include "input.h"
#include "receiver.h"
#include "hmalloc.h"
#include "hlog.h"
#include "cfg.h"
#include "out_mysql.h"
#include "out_json.h"
#include "cache.h"
#include "fingerpori.h"

#ifdef DMALLOC
#include <dmalloc.h>
#endif

int done;

void closedown(int sig)
{
	done = 1;
}

int main(int argc, char *argv[])
{
	int err;
	done = 0;
	snd_pcm_t *handle;
	FILE *sound_in_fd = NULL;
	FILE *sound_out_fd = NULL;
	int channels;
	short *buffer = NULL;
	int buffer_l;
	int buffer_read;
	struct receiver *rx_a = NULL;
	struct receiver *rx_b = NULL;
#ifdef HAVE_PULSEAUDIO
	pa_simple *pa_dev = NULL;
#endif
	
	/* command line */
	parse_cmdline(argc, argv);
	
	/* open syslog, write an initial log message and read configuration */
	open_log(logname, 0);
	hlog(LOG_NOTICE, "Starting up...");
	if (read_config()) {
		hlog(LOG_CRIT, "Initial configuration failed.");
		exit(1);
	}
	
	/* fork a daemon */
	if (fork_a_daemon) {
		int i = fork();
		if (i < 0) {
			hlog(LOG_CRIT, "Fork to background failed: %s", strerror(errno));
			fprintf(stderr, "Fork to background failed: %s\n", strerror(errno));
			exit(1);
		} else if (i == 0) {
			/* child */
		} else {
			/* parent, quitting */
			hlog(LOG_DEBUG, "Forked daemon process %d, parent quitting", i);
			exit(0);
		}
	}
	
	/* write pid file, now that we have our final pid... might fail, which is critical */
	hlog(LOG_DEBUG, "Writing pid...");
	if (!writepid(pidfile))
		exit(1);
	
	signal(SIGINT, closedown);
	
	/* load fingerprints */
	fingerprints_load(fpdir);
	
	/* initialize uplinks */
	if (uplink_config) {
		hlog(LOG_DEBUG, "Initializing jsonout...");
		if (jsonout_init())
			exit(1);
	}
	
	/* initialize the AIS decoders */
	if (sound_channels != SOUND_CHANNELS_MONO) {
		hlog(LOG_DEBUG, "Initializing demodulator A");
		rx_a = init_receiver('A', 2, 0);
		hlog(LOG_DEBUG, "Initializing demodulator B");
		rx_b = init_receiver('B', 2, 1);
		channels = 2;
	} else {
		hlog(LOG_DEBUG, "Initializing demodulator A");
		rx_a = init_receiver('A', 1, 0);
		channels = 1;
	}
	if (sound_device){

		if ((err = snd_pcm_open(&handle, sound_device, SND_PCM_STREAM_CAPTURE, 0)) < 0) {
			hlog(LOG_CRIT, "Error opening sound device (%s)", sound_device);
			return -1;
		}
		
		if (input_initialize(handle, &buffer, &buffer_l) < 0)
			return -1;
	} else if (sound_in_file) {
		if ((sound_in_fd = fopen(sound_in_file, "r")) == NULL) {
			hlog(LOG_CRIT, "Could not open sound file %s: %s", sound_in_file, strerror(errno));
			return -1;
		}
		hlog(LOG_NOTICE, "Reading audio from file: %s", sound_in_file);
		buffer_l = 1024;
		int extra = buffer_l % 5;
		buffer_l -= extra;
		buffer = (short *) hmalloc(buffer_l * sizeof(short) * channels);
	} else {
		hlog(LOG_CRIT, "Neither sound device or sound file configured.");
		return -1;
	}
	
	if (sound_out_file) {
		if ((sound_out_fd = fopen(sound_out_file, "w")) == NULL) {
			hlog(LOG_CRIT, "Could not open sound output file %s: %s", sound_out_file, strerror(errno));
			return -1;
		}
		hlog(LOG_NOTICE, "Recording audio to file: %s", sound_out_file);
	}
	
	hlog(LOG_NOTICE, "Started");
	
	while (!done) {
		if (sound_in_fd) {
			buffer_read = fread(buffer, channels * sizeof(short), buffer_l, sound_in_fd);
			if (buffer_read <= 0)
				done = 1;
		} 
		else {
			buffer_read = input_read(handle, buffer, buffer_l);
			//printf("read %d\n", buffer_read);
		}
		if (buffer_read <= 0)
			continue;
		
		if (sound_out_fd) {
			fwrite(buffer, channels * sizeof(short), buffer_read, sound_out_fd);
		}
		
		if (sound_channels == SOUND_CHANNELS_MONO) {
			receiver_run(rx_a, buffer, buffer_read);
		}
		if (sound_channels == SOUND_CHANNELS_BOTH
		    || sound_channels == SOUND_CHANNELS_RIGHT) {
			/* ch a/0/right */
			receiver_run(rx_a, buffer, buffer_read);
		}
		if (sound_channels == SOUND_CHANNELS_BOTH
		    || sound_channels == SOUND_CHANNELS_LEFT) {	
			/* ch b/1/left */
			receiver_run(rx_b, buffer, buffer_read);
		}
	}
	
	hlog(LOG_NOTICE, "Closing down...");
	if (sound_in_fd) {
		fclose(sound_in_fd);
	}
	else {
		input_cleanup(handle);
		handle = NULL;
	}

	
	if (sound_out_fd)
		fclose(sound_out_fd);
	
	if (uplink_config)
		jsonout_deinit();
	
	hfree(buffer);
	
	free_receiver(rx_a);
	free_receiver(rx_b);
	
	free_config();
	close_log(0);
	
	return 0;
}

