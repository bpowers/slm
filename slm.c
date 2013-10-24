// Copyright 2013 Bobby Powers. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <assert.h>
#include <ftw.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wordexp.h>

#include "utf.h"

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
	size_t size;
	uint8_t data[];
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
static size_t id3_syncsafe(uint8_t const*, size_t);
static Tags *id3_parse(FILE *f);
static Tags *atom_parse(FILE *f);
static size_t utf16to8(char *dst, void *src, size_t len);

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
id3_syncsafe(uint8_t const *b, size_t len)
{
	size_t n = 0;
	for (size_t i = 0; i < len; i++)
		n |= (b[i] & 0x7f) << (len - i - 1)*7;
	return n;
}

ID3Header*
id3_header(FILE *f)
{
	uint8_t buf[ID3_HEADER_LEN];
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
	h->len = 4 * id3_syncsafe(buf+6, 4);

	return h;
err:
	rewind(f);
	return NULL;
}

ID3Frame*
id3_frame(FILE *f, ID3Header* h, size_t max_len)
{
	uint8_t buf[ID3_HEADER_LEN];
	const size_t header_len = h->major == 2 ? 6 : 10;
	const size_t field_len = h->major == 2 ? 3 : 4;
	ID3Frame *fr = NULL;

	size_t n = fread(buf, 1, header_len, f);
	if (n < header_len)
		goto err;

	size_t frame_len = id3_syncsafe(buf+field_len, field_len);
	if (frame_len > max_len) {
		//fprintf(stderr, "max(%d:%x %x %x): expected %zu <= %zu\n", h->major, buf[0], buf[1], buf[2], frame_len, max_len);
		//assert(false);
		goto err;
	}
	fr = calloc(1, sizeof(*fr) + frame_len);

	memcpy(fr->id, buf, field_len);
	fr->size = frame_len;
	n = fread(fr->data, 1, frame_len, f);
	if (n < frame_len) {
		fprintf(stderr, "frame(%d): expected %zu >= got %zu\n", h->major, frame_len, n);
		//assert(false);
		goto err;
	}
	return fr;
err:
	free(fr);
	return NULL;
}


static const Rune surr1 = 0xd800;
static const Rune surr2 = 0xdc00;
static const Rune surr3 = 0xe000;
static const Rune replacement_char = 0xfffd;

bool
is_surrogate(Rune r)
{
	return surr1 <= r && r < surr3;
}

Rune
decoderune16(Rune r1, Rune r2)
{
	return replacement_char;
}

void
print_frame(ID3Frame* fr)
{
	const bool is_unicode = fr->data[0];
	if (is_unicode) {
		size_t n = utf16to8((char*)fr->data, fr->data+1, fr->size - 1);
		if (n == 0)
			return;
	} else {
		memmove(fr->data, fr->data+1, fr->size - 1);
	}
	fprintf(stderr, "%s: %s\n", fr->id, fr->data);
}

// algorithm from go's unicode/utf16 package
size_t
utf16to8(char *dst, void *src, size_t n)
{
	if (n < 2)
		return 0;

	uint16_t *d = (uint16_t*)src;
	size_t off = 0;

	bool leBOM = false;
	if (d[0] == 0xfffe) {
		leBOM = true;
		d++;
		off += 2;
	}
	else if (d[0] == 0xfeff) {
		d++;
		off += 2;
	}
	if (off >= n)
		return 0;

	// specific to id3
	if (d[0] == 0) {
		d++;
		off+=2;
	}
	if (off >= n)
		return 0;

	char *curr = dst;
	size_t dst_len = 0;
	const size_t u16_len = (n - off)/2;
	for (size_t i = 0; i < u16_len; i++) {
		Rune r = d[i];
		char buf[5];
		memset(buf, 0, 5);
		if (surr1 <= r && r < surr2 && i+1 < u16_len && surr2 <= d[i+1] && d[i+1] < surr3) {
			// valid surrogate sequence
			fprintf(stderr, "!!!surr seq\n");
			//Rune r = decoderune16(d[i], d[i+1]);
		} else if (surr1 <= r && r < surr3) {
			// invalid surrogate sequence
			fprintf(stderr, "!**invalsurr seq\n");
		} else {
			if (leBOM) {
				buf[0] = r >> 8;
				buf[1] = r & 0xff;
			} else {
				buf[0] = r & 0xff;
				buf[1] = r >> 8;
			}
			strcpy(curr, buf);
			curr = curr + strlen(curr);
			dst_len++;
		}
	}
	return dst_len;
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

	const size_t fh_len = h->major == 2 ? 6 : 10;
	size_t id3_len = h->len;
	while (id3_len > fh_len) {
		ID3Frame *fr = id3_frame(f, h, id3_len);
		if (!fr)
			break;
		id3_len -= fh_len + fr->size;
		//fprintf(stderr, "see %d.%d %s (len:%d) (id3_len:%zu)\n", h->major, h->minor, fr->id, fr->size, id3_len);

		if (fr->id[0] == 'T')
			print_frame(fr);

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

	fprintf(stderr, "%s\n", fpath);
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
		} else if (arg[0] == '-') {
			fprintf(stderr, "unknown arg '%s'\n", arg);
			usage();
		} else {
			check_entry(arg, NULL, 0, NULL);
			return 0;
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
