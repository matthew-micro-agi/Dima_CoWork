/*
 * Emulator source (DL-26 "emu"): client of cam-emu over the V4L2-like
 * protocol in fwt_cam_proto.h. Set up like a V4L2 capture device (format,
 * buffers, queue all, stream on), then frames are dequeued as they arrive,
 * with the emulator's monotonic timestamp and sequence. Binding to edges is
 * done by nodecap (nc_match, DL-27); the emulator already drew the true index.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "fwt_cam_proto.h"
#include "nc_source.h"

struct src_emu {
	struct nc_source base;
	int      sock;
	uint8_t *mem;
	size_t   mem_len;
	uint32_t buf_size, frame_size;
	int      nbufs;
};

static int e_fd(struct nc_source *s)
{
	return ((struct src_emu *)s)->sock;
}

static void e_on_edge(struct nc_source *s, const struct fwt_trig_msg *edge)
{
	(void)s;
	(void)edge;  /* the emulator gets edges from trigd itself, like a sensor */
}

static int e_dequeue(struct nc_source *s, struct nc_frame *f)
{
	struct src_emu *e = (struct src_emu *)s;
	struct fwt_cam_msg m;

	for (;;) {
		int r = fwt_cam_recv(e->sock, &m, NULL);

		if (r == -EAGAIN || r == -EINTR)
			return 0;
		if (r == 0)
			return -ECONNRESET;
		if (r < 0)
			return r;
		if (m.type == FWT_CAM_ERROR) {
			fprintf(stderr, "nodecap: cam-emu refused buffer %u: %s\n", m.slot,
				strerror(-m.status));
			continue;
		}
		if (m.type != FWT_CAM_FILLED)
			continue;  /* late reply: ignore */
		if ((int)m.slot >= e->nbufs || m.bytesused > e->buf_size) {
			fprintf(stderr, "nodecap: cam-emu: bad frame (slot %u, %u bytes)\n", m.slot,
				m.bytesused);
			continue;
		}
		*f = (struct nc_frame){
			.data = e->mem + (size_t)m.slot * e->buf_size,
			.len = m.bytesused,
			.t_mono_ns = m.t_mono_ns,
			.sequence = m.sequence,
			.slot = (int)m.slot,
		};
		return 1;
	}
}

static void qbuf(struct src_emu *e, int slot)
{
	struct fwt_cam_msg m;

	fwt_cam_msg_init(&m, FWT_CAM_QBUF);
	m.slot = (uint32_t)slot;
	int r = fwt_cam_send(e->sock, &m, -1);

	if (r && r != -EPIPE)  /* emulator gone: dequeue reports it */
		fprintf(stderr, "nodecap: cam-emu queue buffer %d: %s\n", slot, strerror(-r));
}

static void e_release(struct nc_source *s, const struct nc_frame *f)
{
	qbuf((struct src_emu *)s, f->slot);
}

static void e_destroy(struct nc_source *s)
{
	struct src_emu *e = (struct src_emu *)s;

	if (e->sock >= 0) {
		struct fwt_cam_msg m;

		fwt_cam_msg_init(&m, FWT_CAM_STREAMOFF);
		fwt_cam_send(e->sock, &m, -1);  /* closing is enough; be polite */
		close(e->sock);
	}
	if (e->mem)
		munmap(e->mem, e->mem_len);
	free(e);
}

static const struct nc_source_ops emu_ops = {
	.fd = e_fd, .on_edge = e_on_edge, .dequeue = e_dequeue,
	.release = e_release, .destroy = e_destroy,
};

/* Blocking request/reply during setup (no frames flow before STREAMON). */
static int request(struct src_emu *e, struct fwt_cam_msg *m, int *fd)
{
	uint16_t type = m->type;
	int r = fwt_cam_send(e->sock, m, -1);

	if (r)
		return r;
	for (;;) {
		r = fwt_cam_recv(e->sock, m, fd);
		if (r == 0)
			return -ECONNRESET;
		if (r < 0)
			return r;
		if (m->type == type)
			return m->status;
		if (fd && *fd >= 0) {
			close(*fd);
			*fd = -1;
		}
	}
}

struct nc_source *nc_source_emu_new(const char *path, int w, int h, int nbufs)
{
	struct src_emu *e = calloc(1, sizeof(*e));
	struct sockaddr_un a = { .sun_family = AF_UNIX };
	struct fwt_cam_msg m;
	int r, fd = -1;

	if (!e)
		return NULL;
	e->base = (struct nc_source){ .ops = &emu_ops, .name = "emu" };
	e->sock = -1;
	if (strlen(path) >= sizeof(a.sun_path)) {
		fprintf(stderr, "nodecap: cam-emu socket path too long\n");
		goto fail;
	}
	strcpy(a.sun_path, path);
	e->sock = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
	if (e->sock < 0 || connect(e->sock, (struct sockaddr *)&a, sizeof(a)) != 0) {
		fprintf(stderr, "nodecap: connect %s: %s (is cam-emu running?)\n", path,
			strerror(errno));
		goto fail;
	}

	fwt_cam_msg_init(&m, FWT_CAM_FORMAT);
	m.width = (uint32_t)w;
	m.height = (uint32_t)h;
	m.fourcc = FWT_CAM_FOURCC_NV12;
	if ((r = request(e, &m, NULL)) != 0) {
		fprintf(stderr, "nodecap: cam-emu format %dx%d NV12: %s\n", w, h, strerror(-r));
		goto fail;
	}

	fwt_cam_msg_init(&m, FWT_CAM_REQBUFS);
	m.count = (uint32_t)nbufs;
	if ((r = request(e, &m, &fd)) != 0 || fd < 0 || m.count != (uint32_t)nbufs ||
	    m.buf_size < fwt_cam_nv12_size((uint32_t)w, (uint32_t)h)) {
		fprintf(stderr, "nodecap: cam-emu %d buffers: %s\n", nbufs,
			r ? strerror(-r) : "bad reply");
		goto fail;
	}
	e->nbufs = nbufs;
	e->buf_size = m.buf_size;
	e->frame_size = fwt_cam_nv12_size((uint32_t)w, (uint32_t)h);
	e->mem_len = (size_t)nbufs * m.buf_size;
	e->mem = mmap(NULL, e->mem_len, PROT_READ, MAP_SHARED, fd, 0);
	close(fd);
	fd = -1;
	if (e->mem == MAP_FAILED) {
		e->mem = NULL;
		fprintf(stderr, "nodecap: cam-emu buffers: mmap: %s\n", strerror(errno));
		goto fail;
	}

	for (int k = 0; k < nbufs; k++)
		qbuf(e, k);
	fwt_cam_msg_init(&m, FWT_CAM_STREAMON);
	if ((r = request(e, &m, NULL)) != 0) {
		fprintf(stderr, "nodecap: cam-emu stream on: %s\n", strerror(-r));
		goto fail;
	}
	fcntl(e->sock, F_SETFL, fcntl(e->sock, F_GETFL) | O_NONBLOCK);
	fprintf(stderr, "nodecap: cam-emu %s: %dx%d NV12, %d buffers, streaming\n", path, w, h,
		nbufs);
	return &e->base;

fail:
	if (fd >= 0)
		close(fd);
	e_destroy(&e->base);
	return NULL;
}
