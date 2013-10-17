// Copyright 2013 Bobby Powers. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

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
	    "music statistics\n\n" \
	    "slm " VERSION " (c) 2013 Bobby Powers\n", argv0, argv0);
}

int
main(int argc, char *const argv[])
{
	argv0 = argv[0];

	usage();

	return 0;
}
