// Copyright 2013 Bobby Powers. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

void
usage(void)
{
	die("Usage: %s\n" \
	    "music statistics\n", argv0);
}

int
main(int argc, char *const argv[])
{
	for (argv0 = argv[0], argv++, argc--; argc > 0; argv++, argc--) {
		char const* arg = argv[0];
		if (!strcmp("-help", arg)) {
			usage();
		} else {
			fprintf(stderr, "unknown arg '%s'\n", arg);
			usage();
		}
	}

	return 0;
}
