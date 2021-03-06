
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
#include "fingerpori.h"
#include "cJSON.h"


/* FIR coefficients, bands 0 ... 4000 Hz filter, 5000 ... 24000 Hz pass:
 * from scipy import signal
 * b = signal.remez(36, [0, 4000, 5000, 24000], [0, 1], Hz=48000)
 */
static float coeffs_sql[]={
        -3.63643401e-01,   4.42966528e-02,  -1.67480122e-02,
         1.34063018e-02,  -5.09622442e-02,  -2.31586393e-03,
        -5.28037096e-02,   2.57976086e-02,  -1.96545184e-02,
         7.37171662e-02,   5.99298853e-04,   8.74691352e-02,
        -4.56404347e-02,   4.35786023e-02,  -1.69039005e-01,
         8.58638063e-05,  -3.80806415e-01,   4.46237703e-01,
         4.46237703e-01,  -3.80806415e-01,   8.58638063e-05,
        -1.69039005e-01,   4.35786023e-02,  -4.56404347e-02,
         8.74691352e-02,   5.99298853e-04,   7.37171662e-02,
        -1.96545184e-02,   2.57976086e-02,  -5.28037096e-02,
        -2.31586393e-03,  -5.09622442e-02,   1.34063018e-02,
        -1.67480122e-02,   4.42966528e-02,  -3.63643401e-01
};

/* FIR coefficients for txid signal:
 * from scipy import signal
 * b = signal.remez(36, [0, 700, 900, 24000], [1, 0], Hz=48000)
 */
static float coeffs_txid[]={
        0.14712691,  0.07751501, -0.02297688,  0.06114172, -0.00536196,
        0.05092962,  0.00694951,  0.04458397,  0.01570522,  0.0405898 ,
        0.02192375,  0.03815735,  0.02629226,  0.0363804 ,  0.02967061,
        0.03514475,  0.03193517,  0.03362722,  0.03362722,  0.03193517,
        0.03514475,  0.02967061,  0.0363804 ,  0.02629226,  0.03815735,
        0.02192375,  0.0405898 ,  0.01570522,  0.04458397,  0.00694951,
        0.05092962, -0.00536196,  0.06114172, -0.02297688,  0.07751501,
        0.14712691
};

#define COEFFS_L 36 


struct receiver *init_receiver(char name, int num_ch, int ch_ofs)
{
	struct receiver *rx;

	rx = (struct receiver *) hmalloc(sizeof(struct receiver));
	memset(rx, 0, sizeof(struct receiver));

	rx->filter_sql = filter_init(COEFFS_L, coeffs_sql);
	rx->filter_txid = filter_init(COEFFS_L, coeffs_txid);

	rx->name = name;
	rx->num_ch = num_ch;
	rx->ch_ofs = ch_ofs;
	rx->last_levellog = 0;
	
	rx->bufpos = RECEIVER_BUFLEN/4;
	
	int i;
	for (i = 0; i < RECEIVER_BUFLEN; i++) {
		rx->buffer_sql[i] = -16000;
		rx->buffer_txid[i] = 0;
	}
	
	return rx;
}

void free_receiver(struct receiver *rx)
{
	if (rx) {
		filter_free(rx->filter_sql);
		filter_free(rx->filter_txid);
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
	
	//hlog(LOG_DEBUG, "Notification queued"); //: %s", out);
	
	jsonout_push(out);
}

char current_event_id[48];

static void notify_out(int q, struct fingerprint_t *fp, int16_t *samples, int len, int ofs, int last_noise)
{
	event_id(current_event_id, sizeof(current_event_id));
	
	cJSON *no = cJSON_CreateObject();
	
	cJSON_AddStringToObject(no, "id", current_event_id);
	cJSON_AddStringToObject(no, "event", "match");
	cJSON_AddNumberToObject(no, "t", time(NULL));
	cJSON_AddNumberToObject(no, "duration", -1);
	
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
	event_id(current_event_id, sizeof(current_event_id));
	
	cJSON *no = cJSON_CreateObject();
	
	cJSON_AddStringToObject(no, "id", current_event_id);
	cJSON_AddStringToObject(no, "event", "sql");
	cJSON_AddNumberToObject(no, "t", time(NULL));
	cJSON_AddNumberToObject(no, "duration", -1);
	
	cJSON_AddNumberToObject(no, "lnoise", last_noise);
	cJSON_AddItemToObject(no, "rx", cJSON_CreateShortArray(samples, len));
	
	notify_queue(no);
}

static void notify_out_over_end(unsigned long duration)
{
	cJSON *no = cJSON_CreateObject();
	
	cJSON_AddStringToObject(no, "id", current_event_id);
	cJSON_AddStringToObject(no, "event", "sqlend");
	cJSON_AddNumberToObject(no, "t", time(NULL));
	
	cJSON_AddNumberToObject(no, "duration", duration);
	
	notify_queue(no);
}

struct fingerprint_t *fingerprints;

static unsigned long match_single(int16_t *fingerprint, int16_t *samples, int len, unsigned long giveup)
{
	unsigned long dif_sum = 0;
	
	/* calculate absolute difference */
	int i;
	
	for (i = 0; i < len; i++) {
		unsigned long a = abs(samples[i] - fingerprint[i]);
		dif_sum += a *  ((32768-abs(fingerprint[i]))/4096); // a*a ?
		if (dif_sum > giveup)
			return -1;
	}
	
	/* match goodness is the sum of absolute sample differences divided by amount of samples
	 * (length of fingerprint).
	 */
	return dif_sum;
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
	int threshold_weak = 1100;
	int threshold_strong = 600;
	
	struct matchlist_t matches[TOPLEN];
	int matches_c = 0;
	int matches_best = -1;
	unsigned int matches_best_dif = -1;
	int matches_worst = -1;
	unsigned int matches_worst_dif = -1;
	
	for (fp = fingerprints; (fp); fp = fp->next) {
		unsigned long giveup = threshold_weak*(fp->len*2);
		unsigned long dif = -1;
		int x, best_x;
		
		for (x = 0; x < len; x += 1) {
			unsigned long dift = match_single(fp->samples, &samples[x-fp->len], fp->len, giveup);
			if (dift < dif) {
				dif = dift;
				best_x = x;
			}
		}
		
		dif = dif / (fp->len*3);
		
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

#define SAMPLE_RATE 48000

static int sql_open = 0;

#define SQL_ABSVAL_DIVIDER (SAMPLE_RATE/40) /* 0.05s */
#define SQL_ABSVAL_THR_OPEN (1500L*SQL_ABSVAL_DIVIDER)
#define SQL_ABSVAL_THR_CLOSE (3400L*SQL_ABSVAL_DIVIDER)
#define SQL_OPEN_SCAN_LEN SQL_ABSVAL_DIVIDER*2

static int copy_buffer(short *in, short *out, short *in_sql_hp, int step, int len, short *maxval_out)
{
	int id = 0;
	int od = 0;
	short maxval = -32000;
	short minval = 32000;
	long avgval_sum = 0;
	short cur;
	static unsigned long sql_pos;
	static unsigned int sql_red_step;
	static unsigned long sql_open_at_pos;
	short filtered_samples[SQL_OPEN_SCAN_LEN];

	/* start up as squelch closed */
	static unsigned long sql_absval_sum = SQL_ABSVAL_THR_CLOSE;
	
	while (od < len) {
		out[od] = cur = in[id];
		if (cur > maxval)
			maxval = cur;
		if (cur < minval)
			minval = cur;
		avgval_sum += cur;
		
		sql_absval_sum -= (sql_absval_sum/SQL_ABSVAL_DIVIDER);
		short cur_sql = in_sql_hp[od];
		sql_absval_sum += (cur_sql > 0) ? cur_sql : cur_sql * -1;
		
		sql_pos++;
		sql_red_step++;
		if (sql_red_step == 15) {
			sql_red_step = 0;
			
			//hlog(LOG_DEBUG, "checking sql, sql_pre_open %d", sql_pre_open);
			
			if (sql_open && sql_absval_sum > SQL_ABSVAL_THR_CLOSE) {
				unsigned long over_length = ((sql_pos - sql_open_at_pos) * 1000) / SAMPLE_RATE;
				hlog(LOG_INFO, "SQL closed, over length %.3f s", (float)over_length / 1000);
				notify_out_over_end(over_length);
				sql_open = 0;
			}
			
			if (!sql_open && sql_absval_sum < SQL_ABSVAL_THR_OPEN) {
				sql_open = 1;
				sql_open_at_pos = sql_pos;
				hlog(LOG_INFO, "SQL opened");
				
				int newlen = sample_filter_avg(filtered_samples, &out[od-SQL_OPEN_SCAN_LEN], SQL_OPEN_SCAN_LEN);
				
				int last_noise_ofs = SQL_OPEN_SCAN_LEN-SQL_ABSVAL_DIVIDER;
				if (search_fingerprints(filtered_samples, newlen, last_noise_ofs) == NULL)
					notify_out_sql(filtered_samples, newlen, last_noise_ofs);
					//if (search_fingerprints(&out[od-SQL_OPEN_SCAN_LEN], SQL_OPEN_SCAN_LEN, last_noise_ofs) == NULL)
					//	notify_out_sql(&out[od-SQL_OPEN_SCAN_LEN], SQL_OPEN_SCAN_LEN, last_noise_ofs);
			}
		}
		
		id += step;
		od++;
	}

#if 0
	hlog(LOG_DEBUG, "sql_step_avg: sample range: %d ... %d, avg %ld sql lev %ld",
		minval, maxval, avgval_sum/len, sql_absval_sum/SQL_ABSVAL_DIVIDER);
#endif
	
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
		memcpy(rx->buffer_sql, &rx->buffer_sql[rx->bufpos - RECEIVER_BUF_COPY], RECEIVER_BUF_COPY*sizeof(uint16_t));
		rx->bufpos = RECEIVER_BUF_COPY;
	}
	
	/* run highpass filter for squelch */
	short sql_hp_filtered[RECEIVER_BUFLEN];
	filter_run_buf(rx->filter_sql, buf, sql_hp_filtered, rx_num_ch, len);
	
	int copied = copy_buffer(buf, &rx->buffer_sql[rx->bufpos], sql_hp_filtered, rx_num_ch, len, &maxval);
	
	//search_fingerprints(&rx->buffer[rx->bufpos], len);
	rx->bufpos += copied;
	
	/* calculate level, and log it */
	level = (float)maxval / (float)32768 * (float)100;
	level_distance = time(NULL) - rx->last_levellog;
	
	if (level > 99.0 && (level_distance >= 30 || level_distance >= sound_levellog)) {
		hlog(LOG_NOTICE, "Level on ch %d too high: %.0f %%", rx->ch_ofs, level);
		time(&rx->last_levellog);
	} else if (sound_levellog != 0 && level_distance >= sound_levellog) {
		hlog(LOG_INFO, "Level on ch %d: %.0f %%", rx->ch_ofs, level);
		time(&rx->last_levellog);
	}
}

