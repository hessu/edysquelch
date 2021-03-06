#ifndef INC_RECEIVER_H
#define INC_RECEIVER_H

#include <stdio.h>
#include <stdlib.h>
#include <math.h>


struct fingerprint_t {
	char *name;
	int16_t *samples;
	int len;
	unsigned long ampl_avg;
	int ampl_max;
	
	time_t loaded;
	
	struct fingerprint_t *next;
};

extern struct fingerprint_t *fingerprints;

#define RECEIVER_BUFLEN 16384
struct receiver {
	struct filter *filter_sql;
	struct filter *filter_txid;
	char name;
	int num_ch;
	int ch_ofs;
	time_t last_levellog;
	
	int bufpos;
	int16_t buffer_sql[RECEIVER_BUFLEN];
	int16_t buffer_txid[RECEIVER_BUFLEN];
};

extern struct receiver *init_receiver(char name, int num_ch, int ch_ofs);
extern void free_receiver(struct receiver *rx);

extern void receiver_run(struct receiver *rx, short *buf, int len);

#endif
