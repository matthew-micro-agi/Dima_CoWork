/*
 * nodecap v1: one encoded frame per trigger edge, each carrying its index.
 *
 *   trigd edge -> test frame (index drawn in, DL-17) -> appsrc
 *     -> H.265 encoder -> h265parse -> appsink
 *     -> index from PTS (DL-15) -> per-frame SEI (DL-16)
 *     -> recorder: segments, manifest, ring, locks in --rec DIR (DL-18..21)
 *        and/or one .h265 file (--out), per-frame CSV (--log)
 *
 * Encoder: --enc v4l2 (Wave5 on the board, mandatory controls from the bench
 * test) or --enc x265 (software, laptop). The pipeline is built at the first
 * edge, when the period (frame rate) is known.
 *
 * Stops at trigd's END, after --count edges, or on Ctrl-C; then drains the
 * encoder (EOS) and prints a summary. Exit code 0 only if every edge produced
 * exactly one frame with its SEI (no gaps, duplicates, unmatched or dropped)
 * and, with --rec, every frame was recorded.
 *
 * With --rec, lock requests are served on a control socket (rec_ctl.h) while
 * recording; the session number comes from trigd's plan (DL-20).
 */
#include <errno.h>
#include <getopt.h>
#include <inttypes.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <gst/app/gstappsink.h>
#include <gst/app/gstappsrc.h>
#include <gst/gst.h>

#include "fwt_sei.h"
#include "fwt_seqcheck.h"
#include "fwt_stamp.h"
#include "fwt_trig_msg.h"
#include "nc_source.h"
#include "nodecap_core.h"
#include "rec_ctl.h"
#include "recorder.h"

#define SRC_MAX_QUEUED 4  /* frames waiting in appsrc before we drop */

struct opts {
	const char *sock_path, *enc, *out_path, *log_path, *enc_extra, *level;
	const char *rec_dir, *ctl_path, *src_name;
	uint64_t ring_max_mb;
	int seg_seconds, lock_cap_pct;
	int w, h;
	unsigned bitrate;  /* bit/s */
	int64_t count;
	bool no_sei, quiet;
};

struct stats {
	uint64_t edges, pushed, dropped_in, frames_out, unmatched, keyframes, no_vcl;
	uint64_t bytes;
	uint64_t rec_frames, rec_skipped, rec_errors, locks_ok, locks_refused;
};

struct nodecap {
	struct opts o;
	GstElement *pipe, *src, *sink;
	struct nc_source *cam;  /* frame source (DL-26) */
	size_t frame_size;
	struct fwt_stamp_geom geom;

	/* session, from the first edge */
	bool started;
	uint32_t session;
	int64_t t0_ns, period_ns;

	/* shared with the appsink streaming thread */
	GMutex lock;
	struct nc_edge_table table;
	struct fwt_seqcheck chk;
	struct stats st;
	FILE *out, *log;
	struct recorder rec;
	bool rec_on;
	int ctl_fd;
};

static volatile sig_atomic_t g_stop;

static void on_signal(int sig)
{
	(void)sig;
	g_stop = 1;
}

static void usage(const char *prog)
{
	fprintf(stderr,
		"usage: %s [options]\n"
		"  --sock PATH        trigd socket (default %s)\n"
		"  --src test         frame source: test = built-in, one frame per edge (default)\n"
		"  --enc v4l2|x265    encoder: Wave5 (board) or software (laptop) (default v4l2)\n"
		"  --width W --height H   frame size (default 1920x1200)\n"
		"  --bitrate BPS      target bitrate, bit/s (default 5000000)\n"
		"  --enc-extra STR    extra v4l2 controls, appended as \",STR\"\n"
		"  --level L          H.265 level for v4l2 (default 5; \"\" = encoder default)\n"
		"  --rec DIR          record into DIR (e.g. /rec): segments, manifest, ring\n"
		"  --ring-max-mb N    ring size limit (default: filesystem minus reserve)\n"
		"  --seg-seconds N    segment length (default 4)\n"
		"  --lock-cap-pct N   max share of the ring that may be locked (default 20)\n"
		"  --ctl-sock PATH    lock requests while recording (default %s)\n"
		"  --out FILE         also/instead write one H.265 byte stream\n"
		"  --log FILE         per-frame CSV log\n"
		"  --count N          stop after N edges\n"
		"  --no-sei           don't insert the SEI (debug)\n"
		"  -q                 quiet: summary only\n",
		prog, FWT_TRIGD_SOCK_DEFAULT, REC_CTL_SOCK_DEFAULT);
}

static int parse_opts(int argc, char **argv, struct opts *o)
{
	static const struct option lo[] = {
		{ "sock", required_argument, NULL, 'S' },
		{ "src", required_argument, NULL, 'X' },
		{ "enc", required_argument, NULL, 'e' },
		{ "width", required_argument, NULL, 'W' },
		{ "height", required_argument, NULL, 'H' },
		{ "bitrate", required_argument, NULL, 'b' },
		{ "enc-extra", required_argument, NULL, 'x' },
		{ "level", required_argument, NULL, 'L' },
		{ "out", required_argument, NULL, 'o' },
		{ "rec", required_argument, NULL, 'R' },
		{ "ring-max-mb", required_argument, NULL, 'M' },
		{ "seg-seconds", required_argument, NULL, 'G' },
		{ "lock-cap-pct", required_argument, NULL, 'C' },
		{ "ctl-sock", required_argument, NULL, 'K' },
		{ "log", required_argument, NULL, 'l' },
		{ "count", required_argument, NULL, 'n' },
		{ "no-sei", no_argument, NULL, 's' },
		{ "quiet", no_argument, NULL, 'q' },
		{ "help", no_argument, NULL, 'h' },
		{ NULL, 0, NULL, 0 },
	};
	int c;

	*o = (struct opts){ .sock_path = FWT_TRIGD_SOCK_DEFAULT, .enc = "v4l2",
			    .enc_extra = "", .level = "5", .ctl_path = REC_CTL_SOCK_DEFAULT,
			    .src_name = "test",
			    .seg_seconds = 4, .lock_cap_pct = 20,
			    .w = 1920, .h = 1200, .bitrate = 5000000 };
	while ((c = getopt_long(argc, argv, "qh", lo, NULL)) != -1) {
		switch (c) {
		case 'S': o->sock_path = optarg; break;
		case 'X': o->src_name = optarg; break;
		case 'e': o->enc = optarg; break;
		case 'W': o->w = atoi(optarg); break;
		case 'H': o->h = atoi(optarg); break;
		case 'b': o->bitrate = (unsigned)strtoul(optarg, NULL, 0); break;
		case 'x': o->enc_extra = optarg; break;
		case 'L': o->level = optarg; break;
		case 'o': o->out_path = optarg; break;
		case 'R': o->rec_dir = optarg; break;
		case 'M': o->ring_max_mb = strtoull(optarg, NULL, 0); break;
		case 'G': o->seg_seconds = atoi(optarg); break;
		case 'C': o->lock_cap_pct = atoi(optarg); break;
		case 'K': o->ctl_path = optarg; break;
		case 'l': o->log_path = optarg; break;
		case 'n': o->count = strtoll(optarg, NULL, 0); break;
		case 's': o->no_sei = true; break;
		case 'q': o->quiet = true; break;
		default: usage(argv[0]); return -1;
		}
	}
	if (strcmp(o->src_name, "test")) {
		fprintf(stderr, "nodecap: --src must be test\n");
		return -1;
	}
	if (strcmp(o->enc, "v4l2") && strcmp(o->enc, "x265")) {
		fprintf(stderr, "nodecap: --enc must be v4l2 or x265\n");
		return -1;
	}
	if (!o->out_path && !o->rec_dir) {
		fprintf(stderr, "nodecap: give --rec DIR and/or --out FILE\n");
		return -1;
	}
	if (o->w <= 0 || o->h <= 0 || o->w % 2 || o->h % 2) {
		fprintf(stderr, "nodecap: width/height must be positive and even\n");
		return -1;
	}
	return 0;
}

/* ---- output side: appsink streaming thread ---------------------------- */

static void write_frame(struct nodecap *nc, const uint8_t *au, size_t len, int64_t pts,
			int64_t index, const struct nc_edge *e, bool matched, bool key)
{
	uint8_t sei[FWT_SEI_NAL_MAX];
	size_t sei_len = 0, off = len;

	if (!nc->o.no_sei) {
		struct fwt_sei s = {
			.version = FWT_SEI_VERSION,
			.session = matched ? e->session : nc->session,
			.flags = matched ? e->flags : FWT_SEI_F_NO_EDGE,
			.index = index,
			.t_ns = matched ? e->t_ns : nc->t0_ns + pts,
		};

		sei_len = fwt_sei_build(&s, sei, sizeof(sei));
		if (!fwt_sei_insert_offset(au, len, &off)) {
			nc->st.no_vcl++;
			sei_len = 0;
			off = len;
		}
	}
	/* The frame with its SEI, without copying: head, SEI, rest. */
	struct iovec iov[3] = { { (void *)au, off }, { sei, sei_len },
				{ (void *)(au + off), len - off } };

	if (nc->out)
		for (int i = 0; i < 3; i++)
			fwrite(iov[i].iov_base, 1, iov[i].iov_len, nc->out);
	if (nc->rec_on) {
		int r = rec_write_frame(&nc->rec, iov, 3, index, matched ? e->t_ns : nc->t0_ns + pts,
					key);

		if (r == 0)
			nc->st.rec_frames++;
		else if (r == 1)
			nc->st.rec_skipped++;
		else if (++nc->st.rec_errors <= 5)
			fprintf(stderr, "nodecap: recording index %" PRId64 " failed: %s\n", index,
				strerror(-r));
	}
	nc->st.bytes += len + sei_len;

	if (nc->log)
		fprintf(nc->log, "%" PRId64 ",%" PRId64 ",%" PRId64 ",0x%x,%zu,%d,%d\n", index,
			pts, matched ? e->t_ns : -1, matched ? e->flags : 0, len + sei_len, key,
			matched);
}

static GstFlowReturn on_sample(GstAppSink *sink, gpointer user)
{
	struct nodecap *nc = user;
	GstSample *sample = gst_app_sink_pull_sample(sink);

	if (!sample)
		return GST_FLOW_EOS;

	GstBuffer *buf = gst_sample_get_buffer(sample);
	GstMapInfo map;

	if (!buf || !GST_BUFFER_PTS_IS_VALID(buf) || !gst_buffer_map(buf, &map, GST_MAP_READ)) {
		fprintf(stderr, "nodecap: output buffer without PTS or data, skipped\n");
		gst_sample_unref(sample);
		return GST_FLOW_OK;
	}

	/*
	 * Encoders may shift output timestamps (x265enc adds an offset so decode
	 * times stay positive) and record the shift in the segment. Stream time
	 * undoes it and gives back the PTS we pushed.
	 */
	const GstSegment *seg = gst_sample_get_segment(sample);
	guint64 st = seg ? gst_segment_to_stream_time(seg, GST_FORMAT_TIME, GST_BUFFER_PTS(buf))
			 : GST_BUFFER_PTS(buf);
	int64_t pts = (int64_t)(st == GST_CLOCK_TIME_NONE ? GST_BUFFER_PTS(buf) : st);
	int64_t index = 0, missing = 0;
	bool key = !GST_BUFFER_FLAG_IS_SET(buf, GST_BUFFER_FLAG_DELTA_UNIT);
	struct nc_edge e = { 0 };

	g_mutex_lock(&nc->lock);
	nc_index_of_pts(pts, nc->period_ns, &index);

	bool matched = nc_table_take(&nc->table, index, &e);
	enum fwt_seq_result r = fwt_seqcheck_feed(&nc->chk, nc->session, index, &missing);

	nc->st.frames_out++;
	nc->st.keyframes += key;
	if (!matched) {
		if (++nc->st.unmatched <= 5)
			fprintf(stderr, "nodecap: frame index %" PRId64 " (pts %" PRId64
				") has no edge\n", index, pts);
	}
	if (r == FWT_SEQ_GAP)
		fprintf(stderr, "nodecap: GAP: %" PRId64 " frame(s) missing before index %"
			PRId64 "\n", missing, index);
	else if (r == FWT_SEQ_DUP || r == FWT_SEQ_BACKWARD)
		fprintf(stderr, "nodecap: %s: index %" PRId64 "\n", fwt_seq_result_str(r), index);
	write_frame(nc, map.data, map.size, pts, index, &e, matched, key);
	if (!nc->o.quiet)
		printf("frame index %" PRId64 " pts %" PRId64 " bytes %zu%s\n", index, pts,
		       map.size, key ? " key" : "");
	g_mutex_unlock(&nc->lock);

	gst_buffer_unmap(buf, &map);
	gst_sample_unref(sample);
	return GST_FLOW_OK;
}

/* ---- pipeline --------------------------------------------------------- */

static int build_pipeline(struct nodecap *nc)
{
	const struct opts *o = &nc->o;
	int fps = nc_fps_of(nc->period_ns);
	gchar *desc;
	GError *err = NULL;

	/*
	 * Wave5's level comes from caps negotiation (the hevc_level control is
	 * overridden): without a level in the caps it signals an invalid one.
	 * 1920x1200 needs level 5.
	 */
	gchar *lvl = *o->level ? g_strdup_printf("! video/x-h265,level=(string)%s ", o->level)
			       : g_strdup("");

	if (!strcmp(o->enc, "v4l2"))
		desc = g_strdup_printf(
			"appsrc name=src ! v4l2h265enc extra-controls=\"controls,"
			"frame_level_rate_control_enable=1,video_bitrate=%u,"
			"video_bitrate_mode=1,video_gop_size=%d,"
			"prepend_sps_and_pps_to_idr=1%s%s\" %s"
			"! h265parse config-interval=-1 "
			"! video/x-h265,stream-format=byte-stream,alignment=au "
			"! appsink name=sink sync=false",
			o->bitrate, fps, *o->enc_extra ? "," : "", o->enc_extra, lvl);
	else
		desc = g_strdup_printf(
			/* x265enc takes I420, not NV12: convert (laptop only) */
			"appsrc name=src ! videoconvert ! x265enc bitrate=%u key-int-max=%d "
			"speed-preset=ultrafast tune=zerolatency option-string=repeat-headers=1 "
			"! h265parse config-interval=-1 "
			"! video/x-h265,stream-format=byte-stream,alignment=au "
			"! appsink name=sink sync=false",
			o->bitrate / 1000, fps);
	g_free(lvl);

	if (!o->quiet)
		fprintf(stderr, "nodecap: pipeline: %s\n", desc);
	nc->pipe = gst_parse_launch(desc, &err);
	g_free(desc);
	if (!nc->pipe) {
		fprintf(stderr, "nodecap: pipeline: %s\n", err ? err->message : "?");
		g_clear_error(&err);
		return -1;
	}
	nc->src = gst_bin_get_by_name(GST_BIN(nc->pipe), "src");
	nc->sink = gst_bin_get_by_name(GST_BIN(nc->pipe), "sink");

	/* Wave5 (v4l2h265enc) only accepts caps with colorimetry and interlace-mode
	 * set explicitly; a missing field doesn't match. */
	GstCaps *caps = gst_caps_new_simple(
		"video/x-raw", "format", G_TYPE_STRING, "NV12", "width", G_TYPE_INT, o->w,
		"height", G_TYPE_INT, o->h, "framerate", GST_TYPE_FRACTION, fps, 1,
		"pixel-aspect-ratio", GST_TYPE_FRACTION, 1, 1,
		"interlace-mode", G_TYPE_STRING, "progressive",
		"colorimetry", G_TYPE_STRING, "bt709", NULL);

	g_object_set(nc->src, "caps", caps, "format", GST_FORMAT_TIME, "is-live", TRUE,
		     "block", FALSE, NULL);
	gst_caps_unref(caps);

	GstAppSinkCallbacks cb = { .new_sample = on_sample };

	gst_app_sink_set_callbacks(GST_APP_SINK(nc->sink), &cb, nc, NULL);

	if (gst_element_set_state(nc->pipe, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
		fprintf(stderr, "nodecap: pipeline failed to start\n");
		return -1;
	}
	return 0;
}

/* Print errors/warnings from the bus. Returns 1 on EOS, -1 on error, else 0. */
static int poll_bus(struct nodecap *nc, GstClockTime timeout)
{
	GstBus *bus = gst_element_get_bus(nc->pipe);
	int ret = 0;
	GstMessage *m;

	while (ret == 0 && (m = gst_bus_timed_pop_filtered(
				    bus, timeout,
				    GST_MESSAGE_EOS | GST_MESSAGE_ERROR | GST_MESSAGE_WARNING))) {
		GError *err = NULL;
		gchar *dbg = NULL;

		switch (GST_MESSAGE_TYPE(m)) {
		case GST_MESSAGE_EOS:
			ret = 1;
			break;
		case GST_MESSAGE_ERROR:
			gst_message_parse_error(m, &err, &dbg);
			fprintf(stderr, "nodecap: ERROR from %s: %s (%s)\n",
				GST_OBJECT_NAME(m->src), err->message, dbg ? dbg : "");
			ret = -1;
			break;
		default:
			gst_message_parse_warning(m, &err, &dbg);
			fprintf(stderr, "nodecap: warning from %s: %s\n", GST_OBJECT_NAME(m->src),
				err->message);
			break;
		}
		g_clear_error(&err);
		g_free(dbg);
		gst_message_unref(m);
		timeout = 0;
	}
	gst_object_unref(bus);
	return ret;
}

/* ---- input side: trigger edges ---------------------------------------- */

static int start_session(struct nodecap *nc, const struct fwt_trig_msg *m)
{
	nc->session = m->session;
	nc->period_ns = m->period_ns;
	nc->t0_ns = nc_t0_from_edge(m->t_ns, m->index, m->period_ns);
	if (nc_fps_of(nc->period_ns) <= 0) {
		fprintf(stderr, "nodecap: invalid period %" PRId64 " ns\n", m->period_ns);
		return -1;
	}
	fprintf(stderr, "nodecap: session %" PRIu32 ", period %" PRId64 " ns (%d fps), "
		"T0 %" PRId64 ", first index %" PRId64 "\n", nc->session, nc->period_ns,
		nc_fps_of(nc->period_ns), nc->t0_ns, m->index);
	nc->started = true;
	if (nc->rec_on) {
		g_mutex_lock(&nc->lock);
		int r = rec_session_start(&nc->rec, nc->session, nc->t0_ns, nc->period_ns);
		g_mutex_unlock(&nc->lock);

		if (r) {
			fprintf(stderr, "nodecap: recorder refused session %" PRIu32 ": %s\n",
				nc->session, strerror(-r));
			return -1;
		}
	}
	return build_pipeline(nc);
}

/* Push one frame of the source to the encoder, bound to its edge. */
static void push_frame(struct nodecap *nc, const struct nc_frame *f, const struct fwt_trig_msg *m)
{
	struct nc_edge e = { .index = m->index, .t_ns = m->t_ns, .session = m->session,
			     .flags = m->flags };

	if (gst_app_src_get_current_level_buffers(GST_APP_SRC(nc->src)) >= SRC_MAX_QUEUED) {
		nc->st.dropped_in++;
		fprintf(stderr, "nodecap: encoder behind, frame for index %" PRId64
			" dropped\n", m->index);
		return;
	}
	if (f->len != nc->frame_size) {
		fprintf(stderr, "nodecap: frame of %zu bytes, want %zu: dropped\n", f->len,
			nc->frame_size);
		nc->st.dropped_in++;
		return;
	}

	GstBuffer *buf = gst_buffer_new_allocate(NULL, nc->frame_size, NULL);
	GstMapInfo map;

	gst_buffer_map(buf, &map, GST_MAP_WRITE);
	memcpy(map.data, f->data, nc->frame_size);
	if (f->draw_stamp)
		fwt_stamp_draw(map.data, nc->o.w, map.data + (size_t)nc->o.w * nc->o.h, nc->o.w,
			       nc->o.w, nc->o.h, &nc->geom, f->stamp);
	gst_buffer_unmap(buf, &map);

	GST_BUFFER_PTS(buf) = (GstClockTime)nc_pts_of(m->t_ns, nc->t0_ns);
	GST_BUFFER_DURATION(buf) = (GstClockTime)nc->period_ns;

	g_mutex_lock(&nc->lock);
	nc_table_put(&nc->table, &e);
	g_mutex_unlock(&nc->lock);

	if (gst_app_src_push_buffer(GST_APP_SRC(nc->src), buf) != GST_FLOW_OK)
		fprintf(stderr, "nodecap: push for index %" PRId64 " refused\n", m->index);
	else
		nc->st.pushed++;
}

/* Take every ready frame from the source. */
static void drain_source(struct nodecap *nc)
{
	struct nc_frame f;
	int r;

	while ((r = nc->cam->ops->dequeue(nc->cam, &f)) == 1) {
		if (f.has_edge)  /* bound by construction (test source) */
			push_frame(nc, &f, &f.edge);
		nc->cam->ops->release(nc->cam, &f);
	}
	if (r < 0)
		fprintf(stderr, "nodecap: frame source %s: %s\n", nc->cam->name, strerror(-r));
}

static void on_edge(struct nodecap *nc, const struct fwt_trig_msg *m)
{
	if (m->session != nc->session || m->period_ns != nc->period_ns) {
		fprintf(stderr, "nodecap: edge from another session/period ignored\n");
		return;
	}
	nc->st.edges++;
	nc->cam->ops->on_edge(nc->cam, m);
	drain_source(nc);
}

static int send_type(int fd, const struct sockaddr_un *to, uint16_t type)
{
	struct fwt_trig_msg m;

	fwt_trig_msg_init(&m, type);
	return sendto(fd, &m, sizeof(m), 0, (const struct sockaddr *)to, sizeof(*to)) ==
	       (ssize_t)sizeof(m) ? 0 : -1;
}

static int subscribe(const char *path, struct sockaddr_un *trigd)
{
	*trigd = (struct sockaddr_un){ .sun_family = AF_UNIX };
	if (strlen(path) >= sizeof(trigd->sun_path)) {
		fprintf(stderr, "nodecap: socket path too long\n");
		return -1;
	}
	strcpy(trigd->sun_path, path);

	int fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
	struct sockaddr_un self = { .sun_family = AF_UNIX };

	/* Autobind: unique abstract address chosen by the kernel. */
	if (fd < 0 || bind(fd, (struct sockaddr *)&self, sizeof(sa_family_t)) != 0) {
		fprintf(stderr, "nodecap: socket: %s\n", strerror(errno));
		return -1;
	}
	if (send_type(fd, trigd, FWT_TRIG_SUBSCRIBE) != 0) {
		fprintf(stderr, "nodecap: subscribe to %s: %s (is trigd running?)\n", path,
			strerror(errno));
		close(fd);
		return -1;
	}
	fprintf(stderr, "nodecap: subscribed to %s\n", path);
	return fd;
}

static int init_frames(struct nodecap *nc)
{
	nc->frame_size = nc_nv12_size(nc->o.w, nc->o.h);
	if (!fwt_stamp_geom_default(nc->o.w, nc->o.h, &nc->geom)) {
		fprintf(stderr, "nodecap: frame too small for the index stamp\n");
		return -1;
	}
	nc->cam = nc_source_test_new(nc->o.w, nc->o.h);
	if (!nc->cam) {
		fprintf(stderr, "nodecap: frame source %s: out of memory\n", nc->o.src_name);
		return -1;
	}
	return 0;
}

/*
 * Send EOS and wait until the encoder has drained (max 5 s): EOS reached the
 * sink, or every pushed frame came out. Wave5 (v4l2h265enc) doesn't forward
 * EOS, so the frame count is the reliable end on the board.
 */
static int drain(struct nodecap *nc)
{
	gst_app_src_end_of_stream(GST_APP_SRC(nc->src));
	for (int i = 0; i < 50; i++) {
		int r = poll_bus(nc, 100 * GST_MSECOND);

		if (r)
			return r > 0 ? 0 : -1;

		g_mutex_lock(&nc->lock);
		bool all_out = nc->st.frames_out >= nc->st.pushed;
		g_mutex_unlock(&nc->lock);

		if (all_out && i >= 2) {  /* grace for a late EOS */
			fprintf(stderr, "nodecap: all frames out, encoder sent no EOS\n");
			return 0;
		}
	}
	fprintf(stderr, "nodecap: encoder not drained after 5 s\n");
	return -1;
}

/* Control socket for lock requests while recording (rec_ctl.h). */
static int open_ctl(const char *path)
{
	struct sockaddr_un a = { .sun_family = AF_UNIX };
	char dir[sizeof(a.sun_path)];

	if (strlen(path) >= sizeof(a.sun_path))
		return -1;
	strcpy(a.sun_path, path);
	strcpy(dir, path);
	char *slash = strrchr(dir, '/');

	if (slash && slash != dir) {
		*slash = 0;
		mkdir(dir, 0755);  /* fine if it exists */
	}
	unlink(path);

	int fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);

	if (fd < 0 || bind(fd, (struct sockaddr *)&a, sizeof(a)) != 0) {
		fprintf(stderr, "nodecap: control socket %s: %s\n", path, strerror(errno));
		if (fd >= 0)
			close(fd);
		return -1;
	}
	return fd;
}

static void handle_ctl(struct nodecap *nc)
{
	struct rec_ctl_msg m, rep;
	struct sockaddr_un from;
	socklen_t flen = sizeof(from);
	ssize_t n = recvfrom(nc->ctl_fd, &m, sizeof(m), 0, (struct sockaddr *)&from, &flen);

	if (n < 0 || !rec_ctl_valid(&m, n))
		return;
	rec_ctl_init(&rep, REC_CTL_REPLY);
	m.text[sizeof(m.text) - 1] = 0;

	g_mutex_lock(&nc->lock);
	if (m.type == REC_CTL_LOCK) {
		uint32_t session = m.session ? m.session : nc->session;

		rep.result = rec_lock(&nc->rec, session, m.from_idx, m.to_idx, m.ttl_s, m.text,
				      &rep.lock_id, rep.text, sizeof(rep.text));
		if (rep.result == 0) {
			nc->st.locks_ok++;
			snprintf(rep.text, sizeof(rep.text), "locked");
		} else {
			nc->st.locks_refused++;
		}
		rep.session = session;
	} else if (m.type == REC_CTL_UNLOCK) {
		rep.result = rec_unlock(&nc->rec, m.lock_id);
		rep.lock_id = m.lock_id;
		snprintf(rep.text, sizeof(rep.text), "%s", rep.result ? "no such lock" : "unlocked");
	} else {
		rep.result = -EINVAL;
		snprintf(rep.text, sizeof(rep.text), "unknown request");
	}
	g_mutex_unlock(&nc->lock);
	sendto(nc->ctl_fd, &rep, sizeof(rep), MSG_DONTWAIT, (struct sockaddr *)&from, flen);
}

int main(int argc, char **argv)
{
	static struct nodecap nc;  /* large (edge table); zero-initialised */

	gst_init(&argc, &argv);
	if (parse_opts(argc, argv, &nc.o) != 0)
		return 2;

	struct sigaction sa = { .sa_handler = on_signal };

	sigemptyset(&sa.sa_mask);
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);

	g_mutex_init(&nc.lock);
	nc_table_init(&nc.table);
	fwt_seqcheck_init(&nc.chk);
	if (init_frames(&nc) != 0)
		return 1;

	nc.ctl_fd = -1;
	if (nc.o.rec_dir) {
		struct rec_config rc = { .dir = nc.o.rec_dir, .ring_max_bytes = nc.o.ring_max_mb << 20,
					 .reserve_bytes = 64u << 20, .seg_seconds = nc.o.seg_seconds,
					 .lock_cap_pct = nc.o.lock_cap_pct, .log = stderr,
					 /* first segment's expected size: bitrate x length */
					 .min_seg_bytes = (uint64_t)nc.o.bitrate / 8 *
							  (uint64_t)nc.o.seg_seconds };
		int r = rec_open(&nc.rec, &rc);

		if (r) {
			fprintf(stderr, "nodecap: recorder %s: %s%s\n", nc.o.rec_dir, strerror(-r),
				r == -EBUSY ? " (another recorder owns it)" : "");
			return 1;
		}
		nc.rec_on = true;
		nc.ctl_fd = open_ctl(nc.o.ctl_path);  /* locks unavailable if this fails */
	}
	if (nc.o.out_path) {
		nc.out = fopen(nc.o.out_path, "wb");
		if (!nc.out) {
			fprintf(stderr, "nodecap: %s: %s\n", nc.o.out_path, strerror(errno));
			return 1;
		}
	}
	if (nc.o.log_path) {
		nc.log = fopen(nc.o.log_path, "w");
		if (!nc.log) {
			fprintf(stderr, "nodecap: %s: %s\n", nc.o.log_path, strerror(errno));
			return 1;
		}
		fprintf(nc.log, "index,pts_ns,t_edge_ns,flags,bytes,key,matched\n");
	}

	struct sockaddr_un trigd;
	int fd = subscribe(nc.o.sock_path, &trigd);

	if (fd < 0)
		return 1;

	bool ended = false, failed = false;
	int64_t last_index = -1;

	while (!g_stop && !ended && !failed && (!nc.o.count || (int64_t)nc.st.edges < nc.o.count)) {
		struct pollfd p[2] = { { .fd = fd, .events = POLLIN },
				       { .fd = nc.ctl_fd, .events = POLLIN } };

		if (nc.pipe && poll_bus(&nc, 0) != 0) {
			failed = true;
			break;
		}
		if (poll(p, 2, 100) <= 0)
			continue;
		if (p[1].revents & POLLIN)
			handle_ctl(&nc);
		if (!(p[0].revents & POLLIN))
			continue;

		struct fwt_trig_msg m;
		ssize_t n = recv(fd, &m, sizeof(m), 0);

		if (n < 0 || !fwt_trig_msg_valid(&m, n))
			continue;
		if (m.type == FWT_TRIG_END) {
			fprintf(stderr, "nodecap: trigd ended session %" PRIu32 " at index %"
				PRId64 "\n", m.session, m.index);
			last_index = m.index;
			ended = true;
		} else if (m.type == FWT_TRIG_EDGE) {
			if (!nc.started && start_session(&nc, &m) != 0)
				failed = true;
			else
				on_edge(&nc, &m);
		}
	}
	if (!ended)
		send_type(fd, &trigd, FWT_TRIG_UNSUBSCRIBE);
	close(fd);

	if (nc.pipe) {
		if (!failed && drain(&nc) != 0)
			failed = true;
		gst_element_set_state(nc.pipe, GST_STATE_NULL);
	}

	/* A missing frame at the very end shows as END index > last frame index. */
	g_mutex_lock(&nc.lock);
	if (ended && nc.chk.have && last_index > nc.chk.last_index) {
		int64_t miss = last_index - nc.chk.last_index;

		nc.chk.gaps++;
		nc.chk.missing += miss;
		fprintf(stderr, "nodecap: GAP: %" PRId64 " frame(s) missing at the end\n", miss);
	}

	struct stats *s = &nc.st;

	if (nc.rec_on) {
		rec_close(&nc.rec);  /* closes the open segment, ends the session */
		printf("recorder: recorded %" PRIu64 ", skipped %" PRIu64 ", errors %" PRIu64
		       ", segments closed %" PRIu64 ", deleted %" PRIu64 ", locks %" PRIu64
		       " (refused %" PRIu64 ")\n", s->rec_frames, s->rec_skipped, s->rec_errors,
		       nc.rec.st.segments_closed, nc.rec.st.segments_deleted, s->locks_ok,
		       s->locks_refused);
		if (nc.ctl_fd >= 0) {
			close(nc.ctl_fd);
			unlink(nc.o.ctl_path);
		}
	}

	printf("summary: session %" PRIu32 ", edges %" PRIu64 ", pushed %" PRIu64
	       ", dropped %" PRIu64 ", frames %" PRIu64 ", keyframes %" PRIu64
	       ", gaps %" PRId64 " (missing %" PRId64 "), duplicates %" PRId64
	       ", backward %" PRId64 ", unmatched %" PRIu64 ", no-picture %" PRIu64
	       ", bytes %" PRIu64 ", sei %s%s\n",
	       nc.session, s->edges, s->pushed, s->dropped_in, s->frames_out, s->keyframes,
	       nc.chk.gaps, nc.chk.missing, nc.chk.dups, nc.chk.backward, s->unmatched,
	       s->no_vcl, s->bytes, nc.o.no_sei ? "off" : "on", failed ? " [failed]" : "");

	bool ok = !failed && s->frames_out == s->edges && !s->dropped_in && !nc.chk.gaps &&
		  !nc.chk.dups && !nc.chk.backward && !s->unmatched && !s->no_vcl &&
		  (!nc.rec_on || (s->rec_frames == s->frames_out && !s->rec_errors));
	g_mutex_unlock(&nc.lock);

	nc.cam->ops->destroy(nc.cam);
	if (nc.out)
		fclose(nc.out);
	if (nc.log)
		fclose(nc.log);
	return ok ? 0 : 1;
}
