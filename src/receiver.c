
#if HAVE_CONFIG_H
#include "../config.h"
#endif

#include <string.h>
#include <time.h>

#include "receiver.h"
#include "hlog.h"
#include "cfg.h"
#include "hmalloc.h"
#include "filter.h"


static float coeffs[]={
   2.5959e-55, 2.9479e-49, 1.4741e-43, 3.2462e-38, 3.1480e-33,
   1.3443e-28, 2.5280e-24, 2.0934e-20, 7.6339e-17, 1.2259e-13,
   8.6690e-11, 2.6996e-08, 3.7020e-06, 2.2355e-04, 5.9448e-03,
   6.9616e-02, 3.5899e-01, 8.1522e-01, 8.1522e-01, 3.5899e-01,
   6.9616e-02, 5.9448e-03, 2.2355e-04, 3.7020e-06, 2.6996e-08,
   8.6690e-11, 1.2259e-13, 7.6339e-17, 2.0934e-20, 2.5280e-24,
   1.3443e-28, 3.1480e-33, 3.2462e-38, 1.4741e-43, 2.9479e-49,
   2.5959e-55
};
#define COEFFS_L 36 


struct receiver *init_receiver(char name, int num_ch, int ch_ofs)
{
	struct receiver *rx;

	rx = (struct receiver *) hmalloc(sizeof(struct receiver));
	memset(rx, 0, sizeof(struct receiver));

	rx->filter = filter_init(COEFFS_L, coeffs);

	rx->name = name;
	rx->num_ch = num_ch;
	rx->ch_ofs = ch_ofs;
	rx->last_levellog = 0;
	
	rx->bufpos = RECEIVER_BUFLEN/4;

	return rx;
}

void free_receiver(struct receiver *rx)
{
	if (rx) {
		filter_free(rx->filter);
		hfree(rx);
	}
}

struct fingerprint_t *fingerprints;

static unsigned long match_single(int16_t *fingerprint, int16_t *samples, int len, unsigned long giveup)
{
	unsigned long dif_sum = 0;
	
	/* calculate absolute difference */
	int i;
	
	for (i = 0; i < len; i++) {
		dif_sum += abs(samples[i] - fingerprint[i]);
		if (dif_sum > giveup)
			return -1;
	}
	
	/* match goodness is the sum of absolute sample differences divided by amount of samples
	 * (length of fingerprint)
	 */
	return dif_sum / len;
}


static struct fingerprint_t *search_fingerprints(int16_t *samples, int len)
{
	struct fingerprint_t *fp;
	int threshold_weak = 1000;
	int threshold_strong = 900;
	
	for (fp = fingerprints; (fp); fp = fp->next) {
		unsigned long best = -1;
		unsigned long giveup = threshold_weak*fp->len;
		
		int x;
		for (x = 0; x < len; x += 4) {
			unsigned long dif = match_single(fp->samples, &samples[x-fp->len], fp->len, giveup);
			if (dif < best)
				best = dif;
		}
		
		//hlog(LOG_DEBUG, "Best match: %ld", best);
		if (best < threshold_weak) {
			hlog(LOG_INFO, "%s wingerprint match, best %lu: %s",
				(best < threshold_strong) ? "STRONG" : "weak",
				best, fp->name);
		}
	}
	
	return NULL;
}

static int copy_buffer(short *in, short *out, int step, int len, short *maxval_out)
{
	int id = 0;
	int od = 0;
	short maxval = 0;
	
	while (od < len) {
		out[od] = in[id];
		if (in[id] > maxval)
			maxval = in[id];
		
		id += step;
		od++;
	}
	
	*maxval_out = maxval;
	return od;
}

void receiver_run(struct receiver *rx, short *buf, int len)
{
	short maxval = 0;
	int level_distance;
	float level;
	int rx_num_ch = rx->num_ch;
	
	/* len is number of samples available in buffer for each
	 * channels - something like 1024, regardless of number of channels */
	
	buf += rx->ch_ofs;
	
	if (len > RECEIVER_BUFLEN/2)
		abort();
	
#define RECEIVER_BUF_COPY (RECEIVER_BUFLEN/4)
	if (rx->bufpos + len/rx_num_ch > RECEIVER_BUFLEN) {
		memcpy(rx->buffer, &rx->buffer[RECEIVER_BUF_COPY*3], RECEIVER_BUF_COPY*sizeof(uint16_t));
		rx->bufpos = RECEIVER_BUF_COPY;
	}
	
	int copied = copy_buffer(buf, &rx->buffer[rx->bufpos], rx_num_ch, len, &maxval);
	search_fingerprints(&rx->buffer[rx->bufpos], len);
	rx->bufpos += copied;
	
	/* calculate level, and log it */
	level = (float)maxval / (float)32768 * (float)100;
	level_distance = time(NULL) - rx->last_levellog;
	
	if (level > 95.0 && (level_distance >= 30 || level_distance >= sound_levellog)) {
		hlog(LOG_NOTICE, "Level on ch %d too high: %.0f %%", rx->ch_ofs, level);
		time(&rx->last_levellog);
	} else if (sound_levellog != 0 && level_distance >= sound_levellog) {
		hlog(LOG_INFO, "Level on ch %d: %.0f %%", rx->ch_ofs, level);
		time(&rx->last_levellog);
	}
}

