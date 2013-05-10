
/*
 *	out_json.c
 *
 *	(c) Heikki Hannikainen 2008
 *
 *	Send ship position data out in the JSON AIS format:
 *	http://wiki.ham.fi/JSON_AIS.en
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "../config.h"

#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <string.h>

#ifdef HAVE_CURL
#define ENABLE_JSONAIS_CURL
#include <curl/curl.h>
#endif

#include "out_json.h"
#include "hlog.h"
#include "hmalloc.h"
#include "cfg.h"

#ifdef DMALLOC
#include <dmalloc.h>
#endif

#ifdef HAVE_CURL
static pthread_t jsonout_th;
static int jsonout_die = 0;
#endif

#ifdef ENABLE_JSONAIS_CURL

struct que_t {
	const char *s;
	struct que_t *next;
} *out_json_que = NULL;
struct que_t **out_json_tail = &out_json_que;
pthread_mutex_t out_json_que_mut = PTHREAD_MUTEX_INITIALIZER;

/*
 *	a dummy curl response data handler - it'll get to handle whatever
 *	the upstream server gives us back
 */

size_t curl_wdata(void *ptr, size_t size, size_t nmemb, void *stream)
{
	return size * nmemb;
}


/*
 *	send a single json POST request to an upstream server
 */

static int jsonout_post_single(struct curl_httppost *post, const char *url)
{
	CURL *ch;
	CURLcode r;
	struct curl_slist *headers = NULL;
	long retcode = 200;
	
	if (!(ch = curl_easy_init())) {
		hlog(LOG_ERR, "curl_easy_init() returned NULL");
		return 1;
	}
	
	do {
		headers = curl_slist_append(NULL, "Expect:");
		if (!headers) {
			hlog(LOG_ERR, "curl_slist_append for Expect header failed");
			break;
		}
		
		if ((r = curl_easy_setopt(ch, CURLOPT_HTTPPOST, post))) {
			hlog(LOG_ERR, "curl_easy_setopt(CURLOPT_HTTPPOST) failed: %s", curl_easy_strerror(r));
			break;
		}
		
		if ((r = curl_easy_setopt(ch, CURLOPT_URL, url))) {
			hlog(LOG_ERR, "curl_easy_setopt(CURLOPT_URL) failed: %s (%s)", curl_easy_strerror(r), url);
			break;
		}
		
		if ((r = curl_easy_setopt(ch, CURLOPT_HTTPHEADER, headers))) {
			hlog(LOG_ERR, "curl_easy_setopt(CURLOPT_HEADER) failed: %s (%s)", curl_easy_strerror(r), url);
			break;
		}
		
		if ((r = curl_easy_setopt(ch, CURLOPT_WRITEFUNCTION, &curl_wdata))) {
			hlog(LOG_ERR, "curl_easy_setopt(CURLOPT_WRITEFUNCTION) failed: %s", curl_easy_strerror(r));
			break;
		}
		
		if ((r = curl_easy_setopt(ch, CURLOPT_NOPROGRESS, 1))) {
			hlog(LOG_ERR, "curl_easy_setopt(CURLOPT_NOPROGRESS) failed: %s", curl_easy_strerror(r));
			break;
		}
		if ((r = curl_easy_setopt(ch, CURLOPT_VERBOSE, 0))) {
			hlog(LOG_ERR, "curl_easy_setopt(CURLOPT_VERBOSE) failed: %s", curl_easy_strerror(r));
			break;
		}
		
		if ((r = curl_easy_perform(ch))) {
			hlog(LOG_ERR, "curl_easy_perform() failed: %s (%s)", curl_easy_strerror(r), url);
			break;
		}
		
		if ((r = curl_easy_getinfo(ch, CURLINFO_RESPONSE_CODE, &retcode))) {
			hlog(LOG_ERR, "curl_easy_getinfo(CURLINFO_RESPONSE_CODE) failed: %s (%s)", curl_easy_strerror(r), url);
			break;
		}
	} while (0);
	
	curl_easy_cleanup(ch);
	
	if (headers)
		curl_slist_free_all(headers);
	
	if (retcode != 200) {
		hlog(LOG_ERR, "JSON AIS export: server for %s returned %ld\n", url, retcode);
		r = -1;
	}
	
	return (r);
}

/*
 *	Encode an unix timestamp in JSON AIS format
 *
 *	YYYYMMDDHHMMSS
 *	01234567890123
 */

int time_jsonais(time_t *t, char *buf, int buflen)
{
	int i;
	struct tm dt;
	
	/* check that the buffer is large enough - we use
	 * 14 bytes plus the NULL
	 */
	if (buflen < 15) {
		hlog(LOG_ERR, "time_jsonais: not enough space to produce JSON AIS timestamp");
		return -1;
	}
	
	/* thread-safe UTC */
	if (gmtime_r(t, &dt) == NULL) {
		hlog(LOG_ERR, "time_jsonais: gmtime_r failed");
		return -1;
	}
	
	i = snprintf(buf, buflen, "%04d%02d%02d%02d%02d%02d",
		dt.tm_year + 1900,
		dt.tm_mon + 1,
		dt.tm_mday,
		dt.tm_hour,
		dt.tm_min,
		dt.tm_sec);
	
	//hlog(LOG_DEBUG, "time_jsonais: %d => %s", *t, buf);
	
	return i;
}

/*
 *	produce a curl form structure containing the JSON data, and send
 *	it to all upstream servers one by one
 */

static void jsonout_post_all(struct que_t *q)
{
	struct uplink_config_t *up;
	struct curl_httppost *cpost = NULL, *last = NULL;
	struct que_t *qp;
	int n = 0;
	char name[32];
	
	curl_formadd(&cpost, &last,
		CURLFORM_COPYNAME, "what",
		CURLFORM_CONTENTTYPE, "text/plain",
		CURLFORM_PTRCONTENTS, "edy",
		CURLFORM_END);
	
	for (qp = q; (qp); qp = qp->next) {
		snprintf(name, 32, "%d", n);
		curl_formadd(&cpost, &last,
			CURLFORM_COPYNAME, name,
			CURLFORM_CONTENTTYPE, "application/json",
			CURLFORM_PTRCONTENTS, qp->s,
			CURLFORM_END);
		n++;
	}
	
	snprintf(name, 32, "%d", n);
	
	curl_formadd(&cpost, &last,
		CURLFORM_COPYNAME, "c",
		CURLFORM_CONTENTTYPE, "text/plain",
		CURLFORM_PTRCONTENTS, name,
		CURLFORM_END);
	
	
	for (up = uplink_config; (up); up = up->next)
		if (up->proto == UPLINK_JSON)
			jsonout_post_single(cpost, up->url);
	
	curl_formfree(cpost);
}

/*
 *	exporting thread
 */

static void jsonout_thread(void *asdf)
{
	hlog(LOG_DEBUG, "jsonout: thread started");
	
	while (1) {
		if (jsonout_die)
			return;
			
		usleep(200000);
		
		if (out_json_que) {
			//hlog(LOG_DEBUG, "jsonout: grabbing outq");
			pthread_mutex_lock(&out_json_que_mut);
			struct que_t *out = out_json_que;
			out_json_tail = &out_json_que;
			out_json_que = NULL;
			pthread_mutex_unlock(&out_json_que_mut);
			//hlog(LOG_DEBUG, "jsonout: posting");
			jsonout_post_all(out);
		}
	}
}

int jsonout_init(void)
{
	curl_global_init(CURL_GLOBAL_ALL);
	
	if (pthread_create(&jsonout_th, NULL, (void *)jsonout_thread, NULL)) {
		hlog(LOG_CRIT, "pthread_create failed for jsonout_thread");
		return -1;
	}
                
	return 0;
}

int jsonout_deinit(void)
{
	int ret;
	
	/* request death */
	jsonout_die = 1;
	
	if ((ret = pthread_join(jsonout_th, NULL))) {
		hlog(LOG_CRIT, "pthread_join of jsonout_thread failed: %s", strerror(ret));
		return -1;
	}
	
	curl_global_cleanup();
	
	return 0;
}

int jsonout_push(const char *s)
{
	struct que_t *q = hmalloc(sizeof(*q));
	
	q->s = s;
	q->next = NULL;
	
	pthread_mutex_lock(&out_json_que_mut);
	*out_json_tail = q;
	out_json_tail = &q->next;
	pthread_mutex_unlock(&out_json_que_mut);
	
	return 0;
}

#else // ENABLE_JSONAIS_CURL

int jsonout_init(void)
{
	hlog(LOG_CRIT, "jsonout_init: JSON export not available in this build");
	return -1;
}

int jsonout_deinit(void)
{
	return -1;
}

#endif
