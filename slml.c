// Copyright 2013 Bobby Powers. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <ftw.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wordexp.h>

#include "config.h"

char *argv0;

static void die(const char *, ...);

void
die(const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);

	exit(EXIT_FAILURE);
}

int
check_entry(const char *fpath, const struct stat *sb, int typeflag,
	    struct FTW *ftwbuf)
{
	fprintf(stderr, "p: %s\n", fpath);

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
