/*
 * sei-dump: laptop checker for nodecap's H.265 output (DL-02, DL-16, DL-17).
 *
 * Reads an Annex B .h265 file and checks, per picture:
 *   - exactly one of our SEIs precedes it
 *   - SEI indices are continuous (no gap, duplicate, backward step), one session
 * With --pixels (default on): decodes the file with ffmpeg and reads the index
 * drawn into each picture; it must equal that picture's SEI index.
 *
 * Exit code 0 only if every check passed (and --frames N matched, if given).
 */
#include <getopt.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fwt_sei.h"
#include "fwt_seqcheck.h"
#include "fwt_stamp.h"

struct pic {
	bool    has_sei;
	int     n_sei;
	bool    key;
	struct fwt_sei sei;
};

static uint8_t *read_file(const char *path, size_t *len)
{
	FILE *f = fopen(path, "rb");
	uint8_t *buf = NULL;
	long n;

	if (!f)
		return NULL;
	if (fseek(f, 0, SEEK_END) == 0 && (n = ftell(f)) >= 0 && fseek(f, 0, SEEK_SET) == 0) {
		buf = malloc((size_t)n + 1);
		if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) {
			free(buf);
			buf = NULL;
		}
		*len = (size_t)n;
	}
	fclose(f);
	return buf;
}

/* Walk the stream: one struct pic per picture, SEIs attached to the next picture. */
static struct pic *scan(const uint8_t *b, size_t len, size_t *n_pics)
{
	size_t cap = 1024, n = 0, pos = 0;
	struct pic *pics = malloc(cap * sizeof(*pics));
	struct pic pending = { 0 };
	struct fwt_nal nal;

	while (pics && fwt_nal_next(b, len, &pos, &nal)) {
		struct fwt_sei s;

		if (nal.type == FWT_H265_NAL_PREFIX_SEI &&
		    fwt_sei_parse_nal(b + nal.off, nal.len, &s)) {
			pending.n_sei++;
			pending.has_sei = true;
			pending.sei = s;
			continue;
		}
		/* VCL with first_slice_segment_in_pic_flag: a new picture starts. */
		if (nal.type >= 0 && nal.type < 32 && nal.len > 2 && (b[nal.off + 2] & 0x80)) {
			if (n == cap) {
				cap *= 2;
				struct pic *p = realloc(pics, cap * sizeof(*pics));

				if (!p) {
					free(pics);
					return NULL;
				}
				pics = p;
			}
			pending.key = nal.type >= 16 && nal.type <= 23;  /* IRAP */
			pics[n++] = pending;
			pending = (struct pic){ 0 };
		}
	}
	*n_pics = n;
	return pics;
}

/* Frame size via ffprobe. */
static bool probe_size(const char *path, int *w, int *h)
{
	char cmd[4200];
	FILE *p;

	snprintf(cmd, sizeof(cmd),
		 "ffprobe -v error -select_streams v:0 -show_entries stream=width,height "
		 "-of csv=p=0:s=x '%s'", path);
	p = popen(cmd, "r");
	if (!p)
		return false;

	bool ok = fscanf(p, "%dx%d", w, h) == 2;

	pclose(p);
	return ok;
}

/*
 * Decode with ffmpeg (luma only, frames in decode = file order, no drops or
 * duplicates) and compare the drawn index with each picture's SEI.
 * Returns the number of mismatches; *decoded gets the decoded frame count.
 */
static int64_t check_pixels(const char *path, const struct pic *pics, size_t n, bool quiet,
			    size_t *decoded)
{
	int w = 0, h = 0;
	struct fwt_stamp_geom g;
	char cmd[4200];

	*decoded = 0;
	if (!probe_size(path, &w, &h) || !fwt_stamp_geom_default(w, h, &g)) {
		fprintf(stderr, "sei-dump: can't get frame size (ffprobe) or frame too small\n");
		return -1;
	}
	snprintf(cmd, sizeof(cmd),
		 "ffmpeg -v error -i '%s' -fps_mode passthrough -f rawvideo -pix_fmt gray -",
		 path);

	FILE *p = popen(cmd, "r");
	size_t fsz = (size_t)w * h;
	uint8_t *y = malloc(fsz);
	int64_t bad = 0;

	if (!p || !y) {
		free(y);
		if (p)
			pclose(p);
		return -1;
	}
	while (fread(y, 1, fsz, p) == fsz) {
		size_t k = (*decoded)++;
		uint32_t v = 0;
		bool read_ok = fwt_stamp_read(y, w, w, h, &g, &v);

		if (k >= n) {
			bad++;
			continue;
		}
		if (!pics[k].has_sei)
			continue;  /* already counted as missing SEI */
		if (!read_ok || v != (uint32_t)pics[k].sei.index) {
			bad++;
			if (!quiet || bad <= 5)
				printf("PIXEL MISMATCH: picture %zu: SEI index %" PRId64
				       ", drawn %s%" PRIu32 "\n", k, pics[k].sei.index,
				       read_ok ? "" : "unreadable ", v);
		}
	}
	pclose(p);
	free(y);
	return bad;
}

int main(int argc, char **argv)
{
	static const struct option lo[] = {
		{ "no-pixels", no_argument, NULL, 'P' },
		{ "frames", required_argument, NULL, 'n' },
		{ "quiet", no_argument, NULL, 'q' },
		{ "help", no_argument, NULL, 'h' },
		{ NULL, 0, NULL, 0 },
	};
	bool pixels = true, quiet = false;
	int64_t want = -1;
	int c;

	while ((c = getopt_long(argc, argv, "qh", lo, NULL)) != -1) {
		switch (c) {
		case 'P': pixels = false; break;
		case 'n': want = strtoll(optarg, NULL, 0); break;
		case 'q': quiet = true; break;
		default:
			fprintf(stderr, "usage: %s [--no-pixels] [--frames N] [-q] FILE.h265\n",
				argv[0]);
			return 2;
		}
	}
	if (optind != argc - 1) {
		fprintf(stderr, "usage: %s [--no-pixels] [--frames N] [-q] FILE.h265\n", argv[0]);
		return 2;
	}

	const char *path = argv[optind];
	size_t len = 0, n = 0;
	uint8_t *buf = read_file(path, &len);

	if (!buf) {
		fprintf(stderr, "sei-dump: can't read %s\n", path);
		return 1;
	}

	struct pic *pics = scan(buf, len, &n);

	if (!pics) {
		fprintf(stderr, "sei-dump: out of memory\n");
		return 1;
	}

	struct fwt_seqcheck chk;
	uint64_t no_sei = 0, multi_sei = 0, keys = 0, no_edge = 0, sessions = 0;
	uint32_t flags_or = 0, session = 0;
	int64_t first = 0, last = 0;

	fwt_seqcheck_init(&chk);
	for (size_t k = 0; k < n; k++) {
		const struct pic *p = &pics[k];
		int64_t missing = 0;

		keys += p->key;
		if (!p->has_sei) {
			no_sei++;
			printf("NO SEI: picture %zu\n", k);
			continue;
		}
		if (p->n_sei > 1) {
			multi_sei++;
			printf("SEVERAL SEIs: picture %zu (%d)\n", k, p->n_sei);
		}

		enum fwt_seq_result r = fwt_seqcheck_feed(&chk, p->sei.session, p->sei.index,
							  &missing);

		if (r == FWT_SEQ_FIRST || r == FWT_SEQ_NEW_SESSION) {
			sessions++;
			session = p->sei.session;
			if (r == FWT_SEQ_FIRST)
				first = p->sei.index;
		} else if (r == FWT_SEQ_GAP) {
			printf("GAP: %" PRId64 " missing before index %" PRId64 " (picture %zu)\n",
			       missing, p->sei.index, k);
		} else if (r == FWT_SEQ_DUP || r == FWT_SEQ_BACKWARD) {
			printf("%s: index %" PRId64 " (picture %zu)\n", fwt_seq_result_str(r),
			       p->sei.index, k);
		}
		last = p->sei.index;
		flags_or |= p->sei.flags;
		no_edge += !!(p->sei.flags & FWT_SEI_F_NO_EDGE);
		if (!quiet)
			printf("picture %zu: session %" PRIu32 " index %" PRId64 " t %" PRId64
			       " flags 0x%x%s\n", k, p->sei.session, p->sei.index, p->sei.t_ns,
			       p->sei.flags, p->key ? " key" : "");
	}

	size_t decoded = 0;
	int64_t pix_bad = 0;

	if (pixels) {
		pix_bad = check_pixels(path, pics, n, quiet, &decoded);
		if (pix_bad >= 0 && decoded != n) {
			printf("DECODED %zu frames, stream has %zu pictures\n", decoded, n);
			pix_bad++;
		}
	}

	printf("summary: pictures %zu, keyframes %" PRIu64 ", session %" PRIu32
	       " (sessions %" PRIu64 "), index %" PRId64 "..%" PRId64 ", no-sei %" PRIu64
	       ", several-sei %" PRIu64 ", gaps %" PRId64 " (missing %" PRId64
	       "), duplicates %" PRId64 ", backward %" PRId64 ", no-edge %" PRIu64
	       ", flags seen 0x%x, pixels %s",
	       n, keys, session, sessions, first, last, no_sei, multi_sei, chk.gaps,
	       chk.missing, chk.dups, chk.backward, no_edge, flags_or,
	       !pixels ? "not checked" : pix_bad < 0 ? "ERROR" : pix_bad ? "MISMATCH" : "ok");
	if (pixels && pix_bad > 0)
		printf(" (%" PRId64 " bad)", pix_bad);
	if (want >= 0)
		printf(", frames want %" PRId64 " %s", want, (int64_t)n == want ? "ok" : "MISMATCH");
	printf("\n");

	bool ok = n > 0 && !no_sei && !multi_sei && !chk.gaps && !chk.dups && !chk.backward &&
		  !no_edge && sessions == 1 && (!pixels || pix_bad == 0) &&
		  (want < 0 || (int64_t)n == want);

	free(pics);
	free(buf);
	return ok ? 0 : 1;
}
