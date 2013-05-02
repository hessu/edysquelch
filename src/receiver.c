
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

static uint64_t match_single(short *fingerprint, int16_t *samples, int len, int s_step)
{
	uint64_t dif_sum = 0;
	
	/* calculate absolute difference */
	int si = 0;
	int fi;
	
	for (fi = 0; fi < len; fi++) {
		dif_sum += abs((int)samples[si] - (int)fingerprint[fi]);
		si += s_step;
	}
	
	/* match goodness is the sum of absolute sample differences divided by amount of samples
	 * (length of fingerprint)
	 */
	return dif_sum / ((uint64_t)len/100);
}


static struct fingerprint_t *search_fingerprints(int16_t *samples, int len)
{
	int si = 0;
	struct fingerprint_t *fp;
	int threshold = 100;
	
	for (fp = fingerprints; (fp); fp = fp->next) {
		uint64_t dif = match_single(fp->samples, samples, fp->len, 2);
		if (dif < threshold) {
			return fp;
		}
	}
	
	return NULL;
}


int load_fingerprints(const char *path)
{
	int loaded = 0;
	
	return loaded;
}


#define	INC	16
#define FILTERED_LEN 4096

void receiver_run(struct receiver *rx, short *buf, int len)
{
	float out;
	short maxval = 0;
	int level_distance;
	float level;
	int rx_num_ch = rx->num_ch;
	short filtered[FILTERED_LEN];
	int i;
	
	/* len is number of samples available in buffer for each
	 * channels - something like 1024, regardless of number of channels */
	
	buf += rx->ch_ofs;
	
	if (len > FILTERED_LEN)
		abort();
	
	maxval = filter_run_buf(rx->filter, buf, filtered, rx_num_ch, len);
	
	for (i = 0; i < len; i++) {
		
		out = filtered[i];
	}
	
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

