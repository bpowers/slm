// Copyright 2013 Bobby Powers. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <assert.h>
#include <ftw.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wordexp.h>

#include "config.h"

char *argv0;

#define ID3_HEADER_LEN 10

typedef struct {
	int major; // version
	int minor; // version
	bool unsync;
	bool extended;
	bool experimental;
	bool footer;
	int len;
} ID3Header;

typedef struct {
	char id[5];
	int size;
	char data[];
} ID3Frame;

typedef struct {
	char *artist;
	char *album;
	char *title;
	int track;
	int disk;
	int n_disks;
} Tags;

static void die(char const*, ...);
static void free_tags(Tags*);
static bool is_music_file(char const*);
static ID3Header *id3_header(FILE*);
static size_t id3_syncsafe(unsigned char const*);
static Tags *id3_parse(FILE *f);
static Tags *atom_parse(FILE *f);

void
die(char const *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);

	exit(EXIT_FAILURE);
}

void
free_tags(Tags *t)
{
	if (!t)
		return;

	free(t->artist);
	free(t->album);
	free(t->title);
	free(t);
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
id3_syncsafe(unsigned char const*b)
{
	size_t n = 0;
	for (size_t i = 0; i < 4; i++)
		n |= (b[i] & 0x7f) << (4 - i - 1)*7;
	return n;
}

ID3Header*
id3_header(FILE *f)
{
	unsigned char buf[ID3_HEADER_LEN];
	ID3Header *h;

	size_t n = fread(buf, 1, ID3_HEADER_LEN, f);
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
	h->len = id3_syncsafe(buf+6);

	return h;
err:
	rewind(f);
	return NULL;
}

ID3Frame*
id3_frame(FILE *f, size_t max_len)
{
	unsigned char buf[10];
	ID3Frame *fr = NULL;

	size_t n = fread(buf, 1, 10, f);
	if (n < 10)
		goto err;;

	size_t frame_len = id3_syncsafe(buf+4);
	if (frame_len > max_len) {
		fprintf(stderr, "max: expected %zu <= %zu\n", frame_len, max_len);
		//assert(false);
		goto err;
	}
	fr = calloc(1, sizeof(*fr) + frame_len);
	memcpy(fr->id, buf, 4);
	fr->size = frame_len;
	n = fread(fr->data, 1, frame_len, f);
	if (n < frame_len) {
		fprintf(stderr, "frame: expected %zu >= got %zu\n", frame_len, n);
		//assert(false);
		goto err;
	}
	return fr;
err:
	free(fr);
	return NULL;
}

Tags*
id3_parse(FILE *f)
{
	Tags *t = NULL;
	ID3Header *h = id3_header(f);
	if (!h)
		return NULL;

	// none of my 2.7k id3 tagged files have an extended header
	if (h->extended)
		goto out;

	size_t id3_len = h->len;
	while (id3_len > ID3_HEADER_LEN) {
		ID3Frame *fr = id3_frame(f, id3_len);
		if (!fr)
			break;
		id3_len -= ID3_HEADER_LEN + fr->size;
		fprintf(stderr, "see %d.%d %s (len:%d) (id3_len:%zu)\n", h->major, h->minor, fr->id, fr->size, id3_len);

		if (strcmp(fr->id, "TALB") == 0)
			fprintf(stderr, "album: %s\n", fr->data);

		free(fr);
	}
out:
	free(h);
	return t;
}

Tags*
atom_parse(FILE *f)
{
	return NULL;
}

int
check_entry(char const *fpath, const struct stat *sb, int typeflag,
	    struct FTW *ftwbuf)
{
	Tags* t = NULL;
	if (!is_music_file(fpath))
		return 0;

	FILE *f = fopen(fpath, "r");
	if (!f)
		return 0;

	t = id3_parse(f);
	if (!t)
		t = atom_parse(f);
	if (!t)
		goto out;

	// TODO(bp) symlink it up
out:
	free_tags(t);
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
