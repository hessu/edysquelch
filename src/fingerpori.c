
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
                     
#include "receiver.h"
#include "fingerpori.h"
#include "hlog.h"
#include "hmalloc.h"

#define FILTER_AVG_LEN 24
int sample_filter_avg(int16_t *dst, int16_t *src, int len)
{
	int i;
	
	for (i = FILTER_AVG_LEN; i < len; i++) {
		long sum = 0;
		int d;
		
		for (d = i - FILTER_AVG_LEN+1; d <= i; d++)
			sum += src[d];
			
		dst[i-FILTER_AVG_LEN] = sum / FILTER_AVG_LEN;
	}
	
	return i-FILTER_AVG_LEN;
}

unsigned long sample_amplitude_sum(int16_t *src, int len)
{
	unsigned long sum;
	int i;
	
	for (i = 0; i < len; i++)
		sum += abs(src[len]);
	
	return sum;
}

struct fingerprint_t *fingerprint_alloc(void)
{
	struct fingerprint_t *fp = hmalloc(sizeof(*fp));
	
	memset((void *)fp, 0, sizeof(*fp));
	
	return fp;
}

void fingerprint_free(struct fingerprint_t *fp)
{
	hfree(fp->name);
	hfree(fp->samples);
	hfree(fp);
}

static int fingerprint_filematch(const struct dirent *e)
{
	if (e->d_name[0] == '.')
		return 0;
	
	char *s = strstr(e->d_name, ".raw");
	if (!s)
		return 0;
	
	if (e->d_name + strlen(e->d_name) - s == 4)
		return 1;
	
	return 0;
}

static int fingerprint_load(const char *d, const char *f)
{
	int ret = -1;
	int fnlen = strlen(d) + strlen(f) + 3;
	char *fn = hmalloc(fnlen);
	struct fingerprint_t *fp = NULL;
	struct stat st;
	
	snprintf(fn, fnlen, "%s/%s", d, f);
	
	int fd = open(fn, O_RDONLY);
	if (fd < 0) {
		hlog(LOG_ERR, "Failed to open fingerprint %s for reading: %s", fn, strerror(errno));
		goto end;
	}
	
	if (fstat(fd, &st) != 0) {
		hlog(LOG_ERR, "Failed to stat fingerprint file %s: %s", fn, strerror(errno));
		goto close;
	}
	
	fp = fingerprint_alloc();
	fp->name = hstrdup(f);
	
	fp->samples = hmalloc(st.st_size);
	int nread = read(fd, (void *)fp->samples, st.st_size);
	if (nread < 0) {
		hlog(LOG_ERR, "Failed to read from fingerprint file %s: %s", fn, strerror(errno));
		goto close;
	} else if (nread != st.st_size) {
		hlog(LOG_ERR, "Short read from fingerprint file %s: %d != %d", fn, nread, st.st_size);
		goto close;
	}
	
	fp->len = nread/sizeof(fp->samples[0]);
	
	ret = 0;
	
	if (strstr(fn, "unfiltered") != NULL) {
		hlog(LOG_INFO, "Filtering unfiltered fingerprint: %s", fn);
		int16_t *tp = hmalloc(st.st_size);
		fp->len = sample_filter_avg(tp, fp->samples, fp->len);
		memcpy(fp->samples, tp, st.st_size);
		hfree(tp);
	}
	
	fp->ampl_avg = sample_amplitude_sum(fp->samples, fp->len) / fp->len;
	
	hlog(LOG_INFO, "Loaded fingerprint file %s: %d bytes, %d samples, ampl_avg %lu", fn, nread, fp->len, fp->ampl_avg);
	
	/* put to linked list */
	fp->next = fingerprints;
	fingerprints = fp;
	
close:	
	if (close(fd) != 0)
		hlog(LOG_ERR, "Failed to close fingerprint %s after reading: %s", fn, strerror(errno));
end:
	hfree(fn);
	
	if (ret != 0 && fp)
		fingerprint_free(fp);
	
	return ret;
}

int fingerprints_load(const char *dir)
{
	struct dirent **namelist;
	int n;
	
	n = scandir(dir, &namelist, fingerprint_filematch, alphasort);
	if (n < 0) {
		hlog(LOG_ERR, "scandir of '%s' failed: %s", dir, strerror(errno));
	} else {
		while (n--) {
			hlog(LOG_INFO, "Loading: %s", namelist[n]->d_name);
			fingerprint_load(dir, namelist[n]->d_name);
			hfree(namelist[n]);
		}
		hfree(namelist);
	}
	
        return 0;
}


