
#if HAVE_CONFIG_H
#include "../config.h"
#endif

#include <string.h>
#include <time.h>
#include <errno.h>
#include <strings.h>
#include <sys/time.h>

#include "receiver.h"
#include "hlog.h"
#include "cfg.h"
#include "hmalloc.h"
#include "filter.h"
#include "out_json.h"
#include "cJSON.h"


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
	
	int i;
	for (i = 0; i < RECEIVER_BUFLEN; i++)
		rx->buffer[i] = -16000;

	return rx;
}

void free_receiver(struct receiver *rx)
{
	if (rx) {
		filter_free(rx->filter);
		hfree(rx);
	}
}

static int event_id(char *buf, int buflen)
{
	struct timeval tv;
	struct tm tm;
	
	if (gettimeofday(&tv, NULL) != 0) {
		hlog(LOG_ERR, "notify_out: gettimeofday failed: %s", strerror(errno));
		buf[0] = 0;
		
		return -1;
	}
	gmtime_r(&tv.tv_sec, &tm);
	
	int n = snprintf(buf, buflen-1, "%04d%02d%02d-%02d%02d%02d.%03ld",
		tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
		tm.tm_hour, tm.tm_min, tm.tm_sec,
		tv.tv_usec / 1000);
		
	buf[buflen-1] = 0;
	
	return n;
}

static void notify_queue(cJSON *no)
{
	/* the tree is built, print it out to a malloc'ed string */
	char *out = cJSON_PrintUnformatted(no);
	cJSON_Delete(no);
	
	hlog(LOG_DEBUG, "Notification queued"); //: %s", out);
	
	jsonout_push(out);
}

static void notify_out(int q, struct fingerprint_t *fp, int16_t *samples, int len, int ofs, int last_noise)
{
	char id[48];
	
	event_id(id, sizeof(id));
	
	cJSON *no = cJSON_CreateObject();
	
	cJSON_AddStringToObject(no, "id", id);
	cJSON_AddStringToObject(no, "event", "match");
	cJSON_AddNumberToObject(no, "t", time(NULL));
	
	cJSON_AddNumberToObject(no, "q", q);
	cJSON_AddNumberToObject(no, "fplen", fp->len);
	
	cJSON_AddStringToObject(no, "name", fp->name);
	cJSON_AddItemToObject(no, "fp", cJSON_CreateShortArray(fp->samples, fp->len));
	cJSON_AddItemToObject(no, "rx", cJSON_CreateShortArray(samples, len));
	cJSON_AddNumberToObject(no, "rxofs", ofs);
	cJSON_AddNumberToObject(no, "lnoise", last_noise);
	
	notify_queue(no);
}

static void notify_out_sql(int16_t *samples, int len, int last_noise)
{
	char id[48];
	
	event_id(id, sizeof(id));
	
	cJSON *no = cJSON_CreateObject();
	
	cJSON_AddStringToObject(no, "id", id);
	cJSON_AddStringToObject(no, "event", "sql");
	cJSON_AddNumberToObject(no, "t", time(NULL));
	
	cJSON_AddNumberToObject(no, "lnoise", last_noise);
	cJSON_AddItemToObject(no, "rx", cJSON_CreateShortArray(samples, len));
	
	notify_queue(no);
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

#define TOPLEN 5
struct matchlist_t {
	int len;
	int dif;
	int x_ofs;
	struct fingerprint_t *fp;
};

static struct fingerprint_t *search_fingerprints(int16_t *samples, int len, int last_noise)
{
	struct fingerprint_t *fp;
	int threshold_weak = 1000;
	int threshold_strong = 800;
	
	struct matchlist_t matches[TOPLEN];
	int matches_c = 0;
	int matches_best = -1;
	unsigned int matches_best_dif = -1;
	int matches_worst = -1;
	unsigned int matches_worst_dif = -1;
	
	
	for (fp = fingerprints; (fp); fp = fp->next) {
		unsigned long giveup = threshold_weak*600; //fp->len;
		unsigned long dif = -1;
		int x, best_x;
		
		for (x = 0; x < len; x += 1) {
			unsigned long dift = match_single(fp->samples, &samples[x-fp->len], fp->len, giveup);
			if (dift < dif) {
				dif = dift;
				best_x = x;
			}
		}
		
		//hlog(LOG_DEBUG, "Best match: %ld", best);
		if (dif < threshold_weak) {
			hlog(LOG_INFO, "%s wingerprint match, best %lu: %s",
				(dif < threshold_strong) ? "STRONG" : "weak",
				dif, fp->name);
			
			int match_ins = -1;
			if (matches_c < TOPLEN) {
				match_ins = matches_c;
				matches_c++;
				hlog(LOG_DEBUG, "now have %d matches in buffer", matches_c);
			} else {
				if (matches_worst >= 0 && matches_worst_dif < dif) {
					hlog(LOG_DEBUG, "worse match than the worst, not adding to matches");
					continue;
				}
				
				/* find second worst */
				int i;
				for (i = 0; i < matches_c; i++) {
				}
			}
			
			if (dif < matches_best_dif) {
				matches_best = match_ins;
				matches_best_dif = dif;
			}
			if (dif > matches_worst) {
				matches_worst = match_ins;
				matches_worst_dif = dif;
			}
			
			matches[match_ins].len = fp->len;
			matches[match_ins].dif = dif;
			matches[match_ins].fp = fp;
			matches[match_ins].x_ofs = best_x;
		}
	}
	
	if (matches_best >= 0) {
		hlog(LOG_DEBUG, "best match is %d, dif %lu", matches_best, matches_best_dif);
		int add_margin = matches[matches_best].fp->len/2;
		int sample_ofs = matches[matches_best].x_ofs - matches[matches_best].fp->len - add_margin;
		notify_out(matches[matches_best].dif,
			matches[matches_best].fp,
			&samples[sample_ofs],
			matches[matches_best].fp->len*2, add_margin, last_noise-sample_ofs);
			
		return matches[matches_best].fp;
	}
	
	return NULL;
}

static int sql_open = 0;

#define SQL_YRANGE_HIGH 5000
#define SQL_YRANGE_LOW -10000
#define SQL_NUM_STEPS 100

static int copy_buffer(short *in, short *out, int step, int len, short *maxval_out)
{
	int id = 0;
	int od = 0;
	short maxval = 0;
	short cur;
	static unsigned long sql_pos, sql_last_high; // TODO: These are going to overflow, with unseen consequences
	static unsigned long sql_bit, sql_step_avg;
	static unsigned int sql_red_step;
	
	while (od < len) {
		out[od] = cur = in[id];
		if (cur > maxval)
			maxval = cur;
		
		if (sql_bit == 1) {
			if (cur < SQL_YRANGE_LOW) {
				sql_bit = 0;
				sql_last_high = sql_pos;
				
				if (sql_step_avg < 40) {
					sql_step_avg += 1;
				}
				
				if (sql_open && sql_step_avg > 30) {
					sql_open = 0;
					hlog(LOG_INFO, "SQL closed");
				}
			}
		} else {
			if (cur > SQL_YRANGE_HIGH) {
				sql_bit = 1;
			}
		}
		
		sql_pos++;
		sql_red_step++;
		if (sql_red_step == 30) {
			sql_red_step = 0;
			if (sql_step_avg > 0) {
				sql_step_avg -= 1;
			} else {
				if (!sql_open) {
					sql_open = 1;
					hlog(LOG_INFO, "SQL opened, last noise %ld samples ago", sql_pos - sql_last_high);
#define SQL_OPEN_SCAN_LEN 1500
					int last_noise_ofs = SQL_OPEN_SCAN_LEN - (sql_pos-sql_last_high);
					if (search_fingerprints(&out[od-SQL_OPEN_SCAN_LEN], SQL_OPEN_SCAN_LEN, last_noise_ofs) == NULL)
						notify_out_sql(&out[od-SQL_OPEN_SCAN_LEN], SQL_OPEN_SCAN_LEN, last_noise_ofs);
				}
			}
		}
		
		id += step;
		od++;
	}
	
	hlog(LOG_DEBUG, "sql_step_avg: %d", sql_step_avg);
	
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
		memcpy(rx->buffer, &rx->buffer[rx->bufpos - RECEIVER_BUF_COPY], RECEIVER_BUF_COPY*sizeof(uint16_t));
		rx->bufpos = RECEIVER_BUF_COPY;
	}
	
	int copied = copy_buffer(buf, &rx->buffer[rx->bufpos], rx_num_ch, len, &maxval);
	
	//search_fingerprints(&rx->buffer[rx->bufpos], len);
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

