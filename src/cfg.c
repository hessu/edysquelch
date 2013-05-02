/*
 *	(c) Heikki Hannikainen, OH7LZB <hessu@hes.iki.fi>
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
 *	
 */

/*
 *	cfg.c: configuration parsing, based on Tomi's code
 */

#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <sys/types.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <sys/resource.h>

#include "cfg.h"
#include "hmalloc.h"
#include "hlog.h"
#include "cfgfile.h"

#ifdef DMALLOC
#include <dmalloc.h>
#endif

#define HELPS	"Usage: " PACKAGE " [-c cfgfile] [-f (fork)] [-n <logname>] [-e <loglevel>] [-o <logdest>] [-r <logdir>] [-l <inputsoundfile>] [-s <recordsoundfile>] [-h (help)]\n"


char def_cfgfile[] = PROGNAME ".conf";
char def_logname[] = PROGNAME;
char def_sound_device[] = "default";

char *cfgfile = def_cfgfile;
char *pidfile;
char *logdir;	/* access logs go here */

char *logname = def_logname;	/* syslog entries use this program name */

char *sound_device;
char *sound_in_file;
char *sound_out_file;
int sound_channels = SOUND_CHANNELS_MONO;
int sound_levellog = 0;

int fork_a_daemon;	/* fork a daemon */
int stats_interval;
int expiry_interval;

int verbose;

int do_interval(int *dest, int argc, char **argv);
int do_sound_ch(int *dest, int argc, char **argv);

/*
 *	Configuration file commands
 */

#define _CFUNC_ (int (*)(void *dest, int argc, char **argv))

static struct cfgcmd cfg_cmds[] = {
	{ "logdir",		_CFUNC_ do_string,	&logdir			},
	{ "statsinterval",	_CFUNC_ do_interval,	&stats_interval		},
	{ "expiryinterval",	_CFUNC_ do_interval,	&expiry_interval	},

	{ "sounddevice",	_CFUNC_ do_string,	&sound_device		},
	{ "soundinfile",	_CFUNC_ do_string,	&sound_in_file		},
	{ "soundoutfile",	_CFUNC_ do_string,	&sound_out_file		},
	{ "soundchannels",	_CFUNC_ do_sound_ch,	&sound_channels		},
	{ "soundlevellog",	_CFUNC_ do_int,		&sound_levellog	},
	
	{ NULL,			NULL,			NULL			}
};

/*
 *	parse an interval specification
 */
 
time_t parse_interval(char *origs)
{
	time_t t = 0;
	int i;
	char *s, *np, *p, c;
	
	np = p = s = hstrdup(origs);
	
	while (*p) {
		if (!isdigit((int)*p)) {
			c = tolower(*p);
			*p = '\0';
			i = atoi(np);
			if (c == 's')
				t += i;
			else if (c == 'm')
				t += 60 * i;
			else if (c == 'h')
				t += 60 * 60 * i;
			else if (c == 'd')
				t += 24 * 60 * 60 * i;
			np = p + 1;
		}
		p++;
	}
	
	if (*np)
		t += atoi(np);
		
	hfree(s);
	return t;
}


/*
 *	Parse an interval configuration entry
 */

int do_interval(int *dest, int argc, char **argv)
{
	if (argc < 2)
		return -1;
		
	*dest = parse_interval(argv[1]);
	return 0;
}

/*
 *	Parse the soundchannels configuration entry
 */

int do_sound_ch(int *dest, int argc, char **argv)
{
	if (argc < 2)
		return -1;
	
	if (strcasecmp(argv[1], "mono") == 0) {
		*dest = SOUND_CHANNELS_MONO;
	} else if (strcasecmp(argv[1], "both") == 0) {
		*dest = SOUND_CHANNELS_BOTH;
	} else if (strcasecmp(argv[1], "left") == 0) {
		*dest = SOUND_CHANNELS_LEFT;
	} else if (strcasecmp(argv[1], "right") == 0) {
		*dest = SOUND_CHANNELS_RIGHT;
	} else {
		hlog(LOG_CRIT, "SoundChannels value unknown: %s", argv[1]);
		return -1;
	}
	
	return 0;
}

/*
 *	upcase
 */
 
char *strupr(char *s)
{
	char *p;
	
	for (p = s; (*p); p++)
		*p = toupper(*p);
		
	return s;
}

/*
 *	Read configuration files, should add checks for this program's
 *	specific needs and obvious misconfigurations!
 */

int read_config(void)
{
	int failed = 0;
	char *s;
	
	if (read_cfgfile(cfgfile, cfg_cmds))
		return -1;
	
	/* these parameters will only be used when reading the configuration
	 * for the first time.
	 */
	if(log_dir){	/* Check if logdir passed from command line. In that case config file parameter should be ignored*/
		logdir = hstrdup(log_dir);	
	}
	else if(!logdir) { /* Using current directory as default if not given neither in config file or command line */
		hlog(LOG_WARNING, "Config: logdir not defined. Using . as log directory");
		logdir = hstrdup(".");
	}

	if (!sound_in_file && !sound_device) {
		sound_device = def_sound_device;
		hlog(LOG_WARNING, "Config: SoundDevice is not defined - using: %s", sound_device);
	}
	
	if (sound_in_file && sound_device) {
		if (sound_device != def_sound_device)
			hfree(sound_device);
		sound_device = NULL;
	}
	
	if (failed)
		return -1;
	
	if (!pidfile) {
		s = hmalloc(strlen(logdir) + 1 + strlen(logname) + 3 + 2);
		sprintf(s, "%s/%s.pid", logdir, logname);
		
		pidfile = s;
	}
	
	return 0;
}

/*
 *	Free configuration variables
 */

void free_config(void)
{
	hfree(logdir);
	hfree(pidfile);
	
	if (cfgfile != def_cfgfile)
		hfree(cfgfile);
	
	if (logname != def_logname)
		hfree(logname);
	
	if (sound_device != def_sound_device)
		hfree(sound_device);
	
	hfree(sound_in_file);
	hfree(sound_out_file);
}

/*
 *	Parse command line arguments
 */

void parse_cmdline(int argc, char *argv[])
{
	int s, i;
	int failed = 0;
	
	while ((s = getopt(argc, argv, "c:fn:r:e:o:l:s:?h")) != -1) {
	switch (s) {
		case 'c':
			cfgfile = hstrdup(optarg);
			break;
		case 'f':
			fork_a_daemon = 1;
			break;
		case 'n':
			logname = hstrdup(optarg);
			break;
		case 'r':
			log_dir = hstrdup(optarg);
			break;
		case 'e':
			i = pick_loglevel(optarg, log_levelnames);
			if (i > -1)
				log_level = i;
			else {
				fprintf(stderr, "Log level unknown: \"%s\"\n", optarg);
				failed = 1;
			}
			break;
		case 'o':
			i = pick_loglevel(optarg, log_destnames);
			if (i > -1)
				log_dest = i;
			else {
				fprintf(stderr, "Log destination unknown: \"%s\"\n", optarg);
				failed = 1;
			}
			break;
		case 'l':
			sound_in_file = hstrdup(optarg);
			break;
		case 's':
			sound_out_file = hstrdup(optarg);
			break;
		case '?':
		case 'h':
			fprintf(stderr, "%s", VERSTR);
			failed = 1;
	}
	}
	
	if ((log_dest == L_FILE) && (!log_dir)) {
		fprintf(stderr, "Log destination set to 'file' but no log directory specified!\n");
		failed = 1;
	}
	
	if (failed) {
		fputs(HELPS, stderr);
		exit(failed);
	}
}

