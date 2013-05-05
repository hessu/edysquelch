
#ifndef FINGERPORI_H
#define FINGERPORI_H

extern void sample_filter_avg(int16_t *dst, int16_t *src, int len);
extern int fingerprints_load(const char *dir);

#endif
