
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

struct fingerprint_t *fingerprint_alloc(void)
{
	struct fingerprint_t *fp = hmalloc(sizeof(*fp));
	
	memset((void *)fp, 0, sizeof(*fp));
	
	return fp;
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
	
	snprintf(fn, fnlen, "%s/%s", d, f);
	
	int fd = open(fn, O_RDONLY);
	if (fd < 0) {
		hlog(LOG_ERR, "Failed to open fingerprint %s for reading: %s", fn, strerror(errno));
		goto end;
	}
	
	
	
	close(fd);
	
	ret = 0;
end:
	hfree(fn);
	
	return ret;
}

int fingerprints_load(const char *dir)
{
	struct dirent **namelist;
	int n;
	
	n = scandir(dir, &namelist, fingerprint_filematch, alphasort);
	if (n < 0)
		hlog(LOG_ERR, "scandir of '%s' failed: %s", dir, strerror(errno));
	else {
		while (n--) {
			hlog(LOG_INFO, "Loading: %s", namelist[n]->d_name);
			fingerprint_load(dir, namelist[n]->d_name);
			hfree(namelist[n]);
		}
		hfree(namelist);
	}
	
        return 0;
}


