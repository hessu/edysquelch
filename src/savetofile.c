#if HAVE_CONFIG_H
#include "config.h"
#endif


#include <stdio.h>
#include <stdlib.h>

#include "edysquelch.h"
#include "input.h"
#include "receiver.h"
#include "protodec.h"

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
	if (argc != 2) {
		fprintf(stderr, "Usage: %s <soundfilename>\n", argv[0]);
		return -1;
	}
	FILE *soundfile;
	if ((soundfile = fopen(argv[1], "w")) == NULL) {
		printf("Could not open sound file\n");
		return -1;
	}
	int err;
	done = 0;
	snd_pcm_t *handle;
	short *buffer;
	int buffer_l = 1024;
	
	signal(SIGINT, closedown);
	printf("Recording sound to: %s\n", argv[1]);
	if ((err = snd_pcm_open(&handle, "hw:2,0", SND_PCM_STREAM_CAPTURE, 0)) < 0) {
		fprintf(stderr, "Error opening sound device\n");
		return -1;
	}
	if (input_initialize(handle, &buffer, &buffer_l) < 0) {
		return -1;
	}

	while (!done) {
		input_read(handle, buffer, buffer_l);
		fwrite(buffer, 2, buffer_l, soundfile);
	}

	printf("Closing down...\n");
	fclose(soundfile);
	input_cleanup(handle);
	
	return 0;
}
