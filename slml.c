// Copyright 2013 Bobby Powers. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <ftw.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wordexp.h>

#include "config.h"

char *argv0;

typedef struct {
	int major; // version
	int minor; // version
	bool unsync;
	bool extended;
	bool experimental;
	bool footer;
	int len;
} ID3Header;

static void die(char const*, ...);
static bool is_music_file(char const*);
static ID3Header *id3_header(FILE*);
static size_t parse_size(unsigned char const*);

void
die(char const *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);

	exit(EXIT_FAILURE);
}

bool
is_music_file(char const *fpath)
{
	char *ext;
	ext = strrchr(fpath, '.');
	if (!ext)
		return false;
	ext++;

	if (strcmp(ext, "mp3") == 0 ||
	    strcmp(ext, "m4a") == 0 ||
	    strcmp(ext, "ogg") == 0 ||
	    strcmp(ext, "flac") == 0)
		return true;
	return false;
}

size_t
parse_size(unsigned char const*b)
{
	size_t n = 0;
	for (size_t i = 0; i < 4; i++)
		n |= (b[i] & 0x7f) << (4 - i - 1)*7;
	return n;
}

ID3Header*
id3_header(FILE *f)
{
	unsigned char buf[10];
	ID3Header *h;

	size_t n = fread(buf, 1, 10, f);
	if (n < 10)
		goto err;

	if (!(buf[0] == 'I' && buf[1] == 'D' && buf[2] == '3')) {
		goto err;
	}

	h = calloc(1, sizeof(*h));
	h->major = buf[3];
	h->minor = buf[4];
	h->unsync = (buf[5] & (1<<7)) != 0;
	h->extended = (buf[5] & (1<<6)) != 0;
	h->experimental = (buf[5] & (1<<5)) != 0;
	h->footer = (buf[5] & (1<<4)) != 0;
	h->len = parse_size(buf+6);

	return h;
err:
	rewind(f);
	return NULL;
}

int
check_entry(char const *fpath, const struct stat *sb, int typeflag,
	    struct FTW *ftwbuf)
{

	if (!is_music_file(fpath))
		return 0;

	FILE *f = fopen(fpath, "r");
	if (!f)
		return 0;

	ID3Header *h = id3_header(f);
	if (!h)
		goto out;

	fprintf(stderr, "'%s': <%d.%d len:%d>\n", fpath, h->major, h->minor, h->len);
out:
	free(h);
	fclose(f);
	return 0;
}

void
usage(void)
{
	die("Usage: %s\n" \
	    "music statistics\n", argv0);
}

int
main(int argc, char *const argv[])
{
	char* startd;
	wordexp_t w;
	int err;

	for (argv0 = argv[0], argv++, argc--; argc > 0; argv++, argc--) {
		char const* arg = argv[0];
		if (!strcmp("-help", arg)) {
			usage();
		} else {
			fprintf(stderr, "unknown arg '%s'\n", arg);
			usage();
		}
	}

	err = wordexp(MUSIC_DIR, &w, 0);
	if (err)
		die("wordexp(%s): %d\n", MUSIC_DIR, err);
	startd = strdup(w.we_wordv[0]);
	wordfree(&w);

	err = nftw(startd, check_entry, 32, 0);
	if (err)
		die("nftw(%s)\n", startd);

	return 0;
}
