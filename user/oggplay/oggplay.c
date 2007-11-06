/****************************************************************************/

/*
 *	oggplay.c -- Play OGG VORBIS data files
 *
 *	(C) Copyright 2007, Paul Dale (gerg@snapgear.com)
 *	(C) Copyright 1999-2002, Greg Ungerer (gerg@snapgear.com)
 *      (C) Copyright 1997-1997, St�phane TAVENARD
 *          All Rights Reserved
 *
 *	This code is a derivitive of Stephane Tavenard's mpegdev_demo.c.
 *
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 * 
 *	This program is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 * 
 *	You should have received a copy of the GNU General Public License
 *	along with this program; if not, write to the Free Software
 *	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/****************************************************************************/

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <sys/file.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <ivorbiscodec.h>
#include <ivorbisfile.h>
#include <linux/soundcard.h>
#include <sys/resource.h>
#include <config/autoconf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/ioctl.h>
#include <sys/wait.h>

/****************************************************************************/

static int	verbose;
static int	quiet;
static int	lcd_line, lcd_time;
static int	lcdfd = -1;
static int	gotsigusr1;
static int	printtime;
static int	onlytags;
static int	crypto_keylen = 0;
static char	*crypto_key;


/****************************************************************************/

/*
 *	OV data stream support.
 */
static const char	 *trk_filename;
static FILE		 *trk_fd;
static OggVorbis_File	  vf;
static int		  dspfd, dsphw;


/****************************************************************************/

/*
 *	Trivial signal handler, processing is done from the main loop.
 */

static void usr1_handler(int ignore)
{
	gotsigusr1 = 1;
}

/****************************************************************************/

/*
 *	Print out the name on a display device if present.
 */

static void lcdtitle(char **user_comments)
{
	const char	*name;
	int	ivp;
	struct  iovec iv[4];
	char	prebuf[10];
	char	*p;
	int	namelen;

	/* Install a signal handler to allow updates to be forced */
	signal(SIGUSR1, usr1_handler);

	/* Determine the name to display.  We use the tag if it is
	 * present and the basename of the file if not.
	 */
	if (user_comments != NULL && *user_comments != NULL) {
		name = *user_comments;
		namelen = strlen(name);
	} else {
		name = strrchr(trk_filename, '/');
		if (name == NULL)
			name = trk_filename;
		else
			name++;
		p = strchr(name, '.');
		if (p == NULL)
			namelen = strlen(name);
		else
			namelen = p - name;
	}

	if (lcd_line) {
		/* Lock the file so we can access it... */
		if (flock(lcdfd, LOCK_SH | LOCK_NB) == -1)
			return;
		if (lcd_line == 0) {
			prebuf[0] = '\f';
			prebuf[1] = '\0';
		} else if (lcd_line == 1) {
			strcpy(prebuf, "\003\005");
		} else if (lcd_line == 2) {
			strcpy(prebuf, "\003\v\005");
		}

		/*
		 * Now we'll write the title out.  We'll do this atomically
		 * just in case two players decide to coexecute...
		 */
		ivp = 0;
		iv[ivp].iov_len = strlen(prebuf) * sizeof(char);
		iv[ivp++].iov_base = prebuf;
		
		iv[ivp].iov_len = namelen * sizeof(char);
		iv[ivp++].iov_base = (void *)name;
		
		//postbuf = '\n';
		//iv[ivp].iov_len = sizeof(char);
		//iv[ivp++].iov_base = &postbuf;
		writev(lcdfd, iv, ivp);

		/* Finally, unlock it since we've finished. */
		flock(lcdfd, LOCK_UN);
	}
}

/****************************************************************************/

/*
 *	Output time info to display device.
 */

static void lcdtime(time_t sttime)
{
	static time_t	lasttime;
	time_t		t;
	char		buf[15], *p;
	int		m, s;

	t = time(NULL) - sttime;
	if (t != lasttime && flock(lcdfd, LOCK_SH | LOCK_NB) == 0) {
		p = buf;
		*p++ = '\003';
		if (lcd_time == 2)
			*p++ = '\v';
		*p++ = '\005';
		m = t / 60;
		s = t % 60;
		if (s < 0) s += 60;
		sprintf(p, "%02d:%02d", m, s);
		write(lcdfd, buf, strlen(buf));
		flock(lcdfd, LOCK_UN);
	}
	lasttime = t;
}

/****************************************************************************/

/*
 *	Configure DSP engine settings for playing this track.
 */
 
static void setdsp(int fd)
{
	int v;

	v = 44100;
	if (ioctl(fileno(stdout), SNDCTL_DSP_SPEED, &v) < 0) {
		fprintf(stderr, "ioctl(SNDCTL_DSP_SPEED)->%d\n", errno);
		exit(1);
	}

	v = 1;
	if (ioctl(fileno(stdout), SNDCTL_DSP_STEREO, &v) < 0) {
		fprintf(stderr, "ioctl(SNDCTL_DSP_STEREO)->%d\n", errno);
		exit(1);
	}

#if BYTE_ORDER == LITTLE_ENDIAN
	v = AFMT_S16_LE;
#else
	v = AFMT_S16_BE;
#endif
	if (ioctl(fileno(stdout), SNDCTL_DSP_SAMPLESIZE, &v) < 0) {
		fprintf(stderr, "ioctl(SNDCTL_DSP_SAMPLESIZE)->%d\n", errno);
		exit(1);
	}
}


/****************************************************************************/

static void usage(int rc)
{
	printf("usage: oggplay [-hviqzRPZ] [-s <time>] "
		"[-d <device>] [-w <filename>] [-c <key>]"
		"[-l <line> [-t]] ogg-files...\n\n"
		"\t\t-h            this help\n"
		"\t\t-v            verbose stdout output\n"
		"\t\t-i            display file tags and exit\n"
		"\t\t-q            quiet (don't print title)\n"
		"\t\t-R            repeat tracks forever\n"
		"\t\t-z            shuffle tracks\n"
		"\t\t-Z            psuedo-random tracks (implicit -R)\n"
		"\t\t-P            print time to decode/play\n"
		"\t\t-s <time>     sleep between playing tracks\n"
		"\t\t-d <device>   audio device for playback\n"
		"\t\t-D            configure audio device as per a DSP device\n"
		"\t\t-l <line>     display title on LCD line (0,1,2) (0 = no title)\n"
		"\t\t-t <line>     display time on LCD line (1,2)\n"
		"\t\t-c <key>      decrypt using key\n"
		);
	exit(rc);
}

/****************************************************************************/
/* define custom OV decode call backs so that we can handle encrypted content.
 */

static int fread_wrap(unsigned char *ptr, size_t sz, size_t n, FILE *f) {
	if (f == NULL)
		return -1;
	const size_t r = fread(ptr, sz, n, f);
	if (crypto_keylen > 0 && r > 0) {
		long pos = (ftell(f) - r) % crypto_keylen;
		int i;

		for (i=0; i<r; i++) {
			ptr[i] ^= crypto_key[pos++];
			if (pos >= crypto_keylen)
				pos = 0;
		}
	}
	return r;
}

static int fseek_wrap(FILE *f, ogg_int64_t off, int whence){
	if (f == NULL)
		return -1;
	return fseek(f, (int)off, whence);
}

static ov_callbacks ovcb = {
	(size_t (*)(void *, size_t, size_t, void *))	&fread_wrap,
	(int (*)(void *, ogg_int64_t, int))		&fseek_wrap,
	(int (*)(void *))				&fclose,
	(long (*)(void *))				&ftell
};

/****************************************************************************/

static int play_one(const char *file) {
	char		pcmout[65536];
	int		current_section;
	time_t		sttime;
	unsigned long	us;
	struct timeval	tvstart, tvend;
	char		**user_comments = NULL;

	trk_filename = file;

	trk_fd = fopen(trk_filename, "r");

	if (trk_fd == NULL) {
		fprintf(stderr, "ERROR: Unable to open '%s', errno=%d\n",
			trk_filename, errno);
		return 1;
	}
	if (ov_open_callbacks(trk_fd, &vf, NULL, 0, ovcb) < 0) {
		fclose(trk_fd);
		fprintf(stderr, "ERROR: Unable to ov_open '%s', errno=%d\n",
			trk_filename, errno);
		return 1;
	}
	user_comments = ov_comment(&vf, -1)->user_comments;

	if (onlytags) {
		char **ptr = user_comments;
		while (ptr && *ptr) {
			puts(*ptr);
			ptr++;
		}
		return 0;
	}

	if (dsphw)
		setdsp(dspfd);
	if (lcd_line)
		lcdtitle(user_comments);

	gettimeofday(&tvstart, NULL);
	sttime = time(NULL);

	/* We are all set, decode the file and play it */
	for (;;) {
		const long ret = ov_read(&vf, pcmout, sizeof(pcmout), &current_section);
		if (ret == 0)
			break;
		else if (ret < 0) {
			ov_clear(&vf);
			return 1;
		}
		write(dspfd, pcmout, ret);
		if (gotsigusr1) {
			gotsigusr1 = 0;
			if (lcd_line)
				lcdtitle(user_comments);
		}
		if (lcd_time)
			lcdtime(sttime);
	}
	ov_clear(&vf);

	if (printtime) {
		gettimeofday(&tvend, NULL);
		us = ((tvend.tv_sec - tvstart.tv_sec) * 1000000) +
		    (tvend.tv_usec - tvstart.tv_usec);
		printf("Total time = %d.%06d seconds\n",
			(us / 1000000), (us % 1000000));
	}
	return 0;
}

int main(int argc, char *argv[])
{
	int		c, i, j, slptime;
	int		repeat;
 	int		argnr, startargnr, rand, shuffle;
	char		*device, *argvtmp;
	pid_t		pid;

	verbose = 0;
	quiet = 0;
	shuffle = 0;
	rand = 0;
	repeat = 0;
	printtime = 0;
	slptime = 0;
	device = "/dev/dsp";
	dsphw = 0;
	onlytags = 0;

	while ((c = getopt(argc, argv, "?himvqzt:RZPs:d:Dl:Vc:")) >= 0) {
		switch (c) {
		case 'V':
			printf("%s version 1.0\n", argv[0]);
			return 0;
		case 'v':
			verbose++;
			break;
		case 'q':
			verbose = 0;
			quiet++;
			break;
		case 'R':
			repeat++;
			break;
		case 'z':
			shuffle++;
			break;
		case 'Z':
			rand++;
			repeat++;
			break;
		case 'P':
			printtime++;
			break;
		case 's':
			slptime = atoi(optarg);
			break;
		case 'd':
			device = optarg;
			break;
		case 'D':
			dsphw = 1;
			break;
		case 'l':
			lcd_line = atoi(optarg);
			break;
		case 't':
			lcd_time = atoi(optarg);
			break;
		case 'i':
			onlytags = 1;
			break;
		case 'c':
			crypto_key = strdup(optarg);
			crypto_keylen = strlen(crypto_key);
			{	char *p = optarg;
				while (*p != '\0')
					*p++ = '\0';
			}
			break;
		case 'h':
		case '?':
			usage(0);
			break;
		}
	}

	argnr = optind;
	if (argnr >= argc)
		usage(1);
	startargnr = argnr;

	/* Make ourselves the top priority process! */
	setpriority(PRIO_PROCESS, 0, -20);
	srandom(time(NULL) ^ getpid());

	/* Open the audio playback device */
	if ((dspfd = open(device, (O_WRONLY | O_CREAT | O_TRUNC), 0660)) < 0) {
		fprintf(stderr, "ERROR: Can't open output device '%s', "
			"errno=%d\n", device, errno);
		exit(0);
	}

	/* Open LCD device if we are going to use it */
	if ((lcd_line > 0) || (lcd_time > 0)) {
		lcdfd = open("/dev/lcdtxt", O_WRONLY);
		if (lcdfd < 0) {
			lcd_time = 0;
			lcd_line = 0;
		}
	}

nextall:
	/* Shuffle tracks if slected */
	if (shuffle) {
		for (c = 0; (c < 10000); c++) {
			i = (((unsigned int) random()) % (argc - startargnr)) +
				startargnr;
			j = (((unsigned int) random()) % (argc - startargnr)) +
				startargnr;
			argvtmp = argv[i];
			argv[i] = argv[j];
			argv[j] = argvtmp;
		}
	}

nextfile:
	if (rand) {
		argnr = (((unsigned int) random()) % (argc - startargnr)) +
			startargnr;
	}

	pid = fork();
	if (pid == 0) {
		exit(play_one(argv[argnr]));
	} else if (pid > 0) {
		int status;

		for (;;) {
			if (waitpid(pid, &status, 0) == -1)
				if (errno != EINTR)
					break;
		}
	}

	if (slptime)
		sleep(slptime);

	if (++argnr < argc)
		goto nextfile;

	if (repeat) {
		argnr = startargnr;
		goto nextall;
	}

	close(dspfd);
	if (lcdfd >= 0)
		close(lcdfd);
	exit(0);
}

/****************************************************************************/