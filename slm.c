// Copyright 2013 Bobby Powers. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#define _GNU_SOURCE // asprintf
#include <stdio.h>
#undef _GNU_SOURCE

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <ftw.h>
#include <libgen.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wordexp.h>

#include "utf.h"

#include "config.h"

bool verbose;
bool should_hardlink;
char *argv0;
char *destd;

#define ID3_HEADER_LEN 10
#define ATOM_HEADER_LEN 0x20

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
	char data[];
} ID3Frame;

typedef struct {
	char type[5];
} AtomHeader;

typedef struct {
	char *artist;
	char *album;
	char *title;
	int track;
	int disk;
	int n_disks;
} Tags;

static void die(const char *, ...);
static void free_tags(Tags *);
static int mkdirr(const char *, mode_t);
static size_t utf16to8(char *, void *, size_t, bool);

static ID3Header *id3_header(FILE*);
static size_t id3_syncsafe(const uint8_t *, size_t);
static Tags *id3_parse(FILE *);
static void id3_decode_frame(ID3Frame *);
static void id3_normalize_v2(ID3Frame *);

static AtomHeader *atom_header(FILE *);
static Tags *atom_parse(FILE *);

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
free_tags(Tags *t)
{
	if (!t)
		return;

	free(t->artist);
	free(t->album);
	free(t->title);
	free(t);
}

/// mkdirr is a recursive mkdir wrapper
int
mkdirr(const char *path, mode_t mode)
{
	char *dirdup, *dir;
	struct stat info;
	int err;

	dirdup = strdup(path);
	dir = dirname(dirdup);

	if (access(dir, F_OK) == -1) {
		if (errno != ENOENT)
			goto error;
		// need to recurse with a copy of dir, becuase basename
		// and dirname can modify the string passed in.
		err = mkdirr(dirdup, mode);
		if (err)
			goto error;
	}

	if (stat(path, &info) == -1) {
		if (mkdir(path, mode) == -1)
			die("%s: mkdir failed for '%s'", __func__, path);
		if (stat(path, &info) == -1)
			die("%s: stat failed for '%s'", __func__, path);
	}

	if (S_ISDIR(info.st_mode)) {
		free(dirdup);
		return 0;
	}

	die("%s: path '%s' not a directory", __func__, path);
error:
	free(dirdup);
	return -1;
}

// algorithm from go's unicode/utf16 package
size_t
utf16to8(char *dst, void *src, size_t n, bool leBOM)
{
	if (n < 2)
		return 0;

	uint8_t *d = src;
	size_t off = 0;

	char *curr = dst;
	size_t dst_len = 0;
	const size_t u16_len = n - off;
	for (size_t i = 0; i < u16_len; i+=2) {
		Rune r;
		if (leBOM)
			r = d[i] + (d[i+1] << 8);
		else
			r = d[i+1] + (d[i] << 8);
		size_t rlen = runetochar(curr, &r);
		curr += rlen;
		dst_len += rlen;
	}
	return dst_len;
}

size_t
id3_syncsafe(const uint8_t *b, size_t len)
{
	size_t n = 0;
	// b[i] SHOULD be masked with 0x7f in this loop if people
	// followed the spec, but spec compliance is the exception,
	// not the rule.
	for (size_t i = 0; i < len; i++)
		n |= b[i] << (len - i - 1)*7;
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

	size_t frame_len;
	if (h->major == 2)
		frame_len = buf[3] << 16 | buf[4] << 8 | buf[5];
	else if (h->major == 3)
		frame_len = buf[4] << 24 | buf[5] << 16 | buf[6] << 8 | buf[7];
	else
		frame_len = id3_syncsafe(buf+field_len, field_len);

	// tag id MUST be ASCII
	if (!isalnum(buf[0]) || !isalnum(buf[1]) || !isalnum(buf[2]) ||
	    frame_len > max_len)
		goto err;

	// +1 to ensure a trailing null
	fr = calloc(1, sizeof(*fr) + frame_len + 1);

	memcpy(fr->id, buf, field_len);
	fr->size = frame_len;

	n = fread(fr->data, 1, frame_len, f);
	if (n < frame_len) {
		//fprintf(stderr, "frame(%d): expected %zu >= got %zu\n", h->major, frame_len, n);
		assert(false);
		goto err;
	}
	return fr;
err:
	free(fr);
	return NULL;
}

void
id3_decode_frame(ID3Frame *fr)
{
	const bool is_unicode = fr->data[0];
	if (!is_unicode) {
		memmove(fr->data, fr->data+1, fr->size - 1);
		fr->data[fr->size-1] = '\0';
		return;
	}

	if (fr->size < 3)
		return;
	uint8_t *d = (uint8_t*)fr->data+1;
	size_t off = 1;
	bool leBOM = false;
	if (d[0] == 0xff && d[1] == 0xfe) {
		leBOM = true;
		d+=2;
		off += 2;
	}
	else if (d[0] == 0xfe && d[1] == 0xff) {
		d+=2;
		off += 2;
	}
	if (off >= fr->size)
		return;
	// leading UTF-16 NULL is used to indicate null
	// termination of string.
	if (d[0] == 0 && d[1] == 0) {
		d++;
		off+=2;
	}
	if (off >= fr->size)
		return;
	size_t n = utf16to8((char*)fr->data, d, fr->size - off, leBOM);
	if (n != 0)
		fr->data[n] = '\0';
}

/// id3_normalize_v2 converts v2 frame ids we care about into the
/// corresponding v3 frame ids.
void
id3_normalize_v2(ID3Frame *fr)
{
	if (strcmp(fr->id, "TT2") == 0)
		strcpy(fr->id, "TIT2");
	else if (strcmp(fr->id, "TP1") == 0)
		strcpy(fr->id, "TPE1");
	else if (strcmp(fr->id, "TAL") == 0)
		strcpy(fr->id, "TALB");
	else if (strcmp(fr->id, "TRK") == 0)
		strcpy(fr->id, "TRCK");
}

Tags*
id3_parse(FILE *f)
{
	Tags *t = NULL;
	ID3Header *h = id3_header(f);
	if (!h)
		return NULL;

	t = calloc(1, sizeof(*t));

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
		if (fr->size == 0) {
			free(fr);
			break;
		}
		if (h->major == 2)
			id3_normalize_v2(fr);

		if (fr->id[0] == 'T') {
			id3_decode_frame(fr);
			if (strcmp(fr->id, "TIT2") == 0)
				t->title = strdup(fr->data);
			else if (strcmp(fr->id, "TPE1") == 0)
				t->artist = strdup(fr->data);
			else if (strcmp(fr->id, "TALB") == 0)
				t->album = strdup(fr->data);
			else if (strcmp(fr->id, "TRCK") == 0)
				t->track = atoi(fr->data);
			else if (strcmp(fr->id, "TPOS") == 0) {
				t->disk = atoi(fr->data);
				char *slash = strchr(fr->data, '/');
				if (slash)
					t->n_disks = atoi(slash+1);
			}
		}

		free(fr);
	}
out:
	free(h);
	return t;
}

AtomHeader*
atom_header(FILE *f)
{
	uint8_t buf[ATOM_HEADER_LEN];
	AtomHeader *h;

	size_t n = fread(buf, 1, ATOM_HEADER_LEN, f);
	if (n < ATOM_HEADER_LEN)
		goto err;

	if (!(buf[4] == 'f' && buf[5] == 't' &&
	      buf[6] == 'y' && buf[7] == 'p')) {
		goto err;
	}

	h = calloc(1, sizeof(*h));

	return h;
err:
	rewind(f);
	return NULL;
}

Tags*
atom_parse(FILE *f)
{
	Tags *t = NULL;
	AtomHeader *h = atom_header(f);
	if (!h)
		return NULL;

	free(h);
	return t;
}

void
link_up(const char *fpath, Tags *t)
{
	int err;
	char *dir = NULL, *track_name = NULL, *new_path = NULL;
	const char *ext = strrchr(fpath, '.')+1;

	if (!t->title)
		return;

	// TODO(bp) compilations

	// TODO(bp) disk prefix
	err = asprintf(&track_name, "%d_%s.%s", t->track, t->title, ext);
	if (err == -1)
		goto error; // FIXME(bp) log

	if (t->album) {
		err = asprintf(&dir, "%s/albums/%s", destd, t->album);
		if (err == -1)
			goto error;
		err = asprintf(&new_path, "%s/%s", dir, track_name);
		if (err == -1)
			goto error;
		err = mkdirr(dir, 0755);
		if (err)
			goto error;
		if (should_hardlink)
			err = link(fpath, new_path);
		else
			err = symlink(fpath, new_path);
	} else if (verbose) {
		fprintf(stderr, "no album link for %s\n", fpath);
	}

	free(dir);
	dir = NULL;
	free(new_path);
	new_path = NULL;

	if (t->artist && t->album) {
		err = asprintf(&dir, "%s/artists/%s/%s", destd, t->artist, t->album);
		if (err == -1)
			goto error;
		err = asprintf(&new_path, "%s/%s", dir, track_name);
		if (err == -1)
			goto error;
		err = mkdirr(dir, 0755);
		if (err)
			goto error;
		if (should_hardlink)
			err = link(fpath, new_path);
		else
			err = symlink(fpath, new_path);
	} else if (verbose) {
		fprintf(stderr, "no artist link for %s\n", fpath);
	}
error:
	free(dir);
	free(track_name);
	free(new_path);
}

int
check_entry(const char *fpath, const struct stat *sb, int typeflag,
	    struct FTW *ftwbuf)
{
	struct stat info;
	Tags *t = NULL;

	if (stat(fpath, &info) == -1 || !S_ISREG(info.st_mode))
		return 0;

	FILE *f = fopen(fpath, "r");
	if (!f)
		return 0;

	t = id3_parse(f);
	if (!t)
		t = atom_parse(f);
	if (!t) {
		if (verbose)
			fprintf(stderr, "no tags for %s\n", fpath);
		goto out;
	}

	link_up(fpath, t);
out:
	free_tags(t);
	fclose(f);
	return 0;
}

void
usage(void)
{
	die("Usage: %s [OPTION...]\n" \
	    "music curation\n\n" \
	    "Options:\n" \
	    "  -v:\tVerbose mode\n" \
	    "  -h:\tUse hardlinks instead of symlinks\n", argv0);
}

int
main(int argc, char *const argv[])
{
	char* startd;
	wordexp_t w;
	int err;

	for (argv0 = argv[0], argv++, argc--; argc > 0; argv++, argc--) {
		char const* arg = argv[0];
		if (strcmp("-help", arg) == 0) {
			usage();
		} else if (strcmp("-v", arg) == 0) {
			verbose = true;
		} else if (strcmp("-h", arg) == 0) {
			should_hardlink = true;
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

	err = wordexp(FARM_DIR, &w, 0);
	if (err)
		die("wordexp(%s): %d\n", FARM_DIR, err);
	destd = strdup(w.we_wordv[0]);
	wordfree(&w);

	err = nftw(startd, check_entry, 32, 0);
	if (err)
		die("nftw(%s)\n", startd);

	free(destd);
	free(startd);
	return 0;
}
