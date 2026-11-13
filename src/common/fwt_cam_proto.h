/*
 * Camera emulator protocol (DL-26): cam-emu <-> nodecap, shaped like V4L2
 * capture so the same client code maps onto a real /dev/videoN later.
 *
 * Transport: Unix SOCK_SEQPACKET socket (one message per packet, connection
 * oriented: the emulator sees the client go away). One client at a time.
 *
 *   client -> emu             emu -> client               V4L2 equivalent
 *   FORMAT  w, h, fourcc      FORMAT  status              VIDIOC_S_FMT
 *   REQBUFS count             REQBUFS status, count,      VIDIOC_REQBUFS + mmap
 *                                     buf_size + fd (*)
 *   QBUF    slot              ERROR   (only on failure)   VIDIOC_QBUF
 *   STREAMON                  STREAMON status             VIDIOC_STREAMON
 *   STREAMOFF                 STREAMOFF status            VIDIOC_STREAMOFF
 *                             FILLED  slot, sequence,     VIDIOC_DQBUF
 *                                     t_mono_ns, bytesused
 *
 * (*) one shared-memory file of count * buf_size bytes, passed with
 *     SCM_RIGHTS; buffer k starts at k * buf_size. REQBUFS count 0 frees it.
 *
 * Buffer ownership as in V4L2: after REQBUFS all buffers belong to the client;
 * QBUF hands one to the emulator; FILLED hands it back. With no buffer queued
 * when a frame is due, the frame is lost and the sequence still advances.
 * STREAMON resets the sequence to 0; STREAMOFF returns all queued buffers.
 *
 * Internal IPC between our own binaries: fixed C struct, host byte order.
 */
#ifndef FWT_CAM_PROTO_H
#define FWT_CAM_PROTO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FWT_CAM_MAGIC    0x4d414346u  /* "FCAM" in memory (little endian) */
#define FWT_CAM_VERSION  1

#define FWT_CAMEMU_SOCK_DEFAULT "/run/fwtrial/cam-emu.sock"

#define FWT_CAM_FOURCC_NV12 0x3231564eu  /* 'N' 'V' '1' '2' */
#define FWT_CAM_MAX_BUFS    32

/* Value drawn into a frame that belongs to no trigger edge (DL-26 ground truth). */
#define FWT_CAM_STAMP_SPURIOUS 0xffffffffu

enum fwt_cam_type {
	FWT_CAM_FORMAT    = 1,
	FWT_CAM_REQBUFS   = 2,
	FWT_CAM_QBUF      = 3,
	FWT_CAM_STREAMON  = 4,
	FWT_CAM_STREAMOFF = 5,
	FWT_CAM_FILLED    = 6,
	FWT_CAM_ERROR     = 7,  /* reply to a failed QBUF: status, slot */
};

struct fwt_cam_msg {
	uint32_t magic;      /* FWT_CAM_MAGIC */
	uint16_t version;    /* FWT_CAM_VERSION */
	uint16_t type;       /* enum fwt_cam_type */
	int32_t  status;     /* replies: 0 or -errno */
	uint32_t slot;       /* QBUF, FILLED, ERROR */
	uint32_t count;      /* REQBUFS: buffers asked for / granted */
	uint32_t width;      /* FORMAT */
	uint32_t height;     /* FORMAT */
	uint32_t fourcc;     /* FORMAT: FWT_CAM_FOURCC_NV12 only */
	uint32_t buf_size;   /* REQBUFS reply: bytes per buffer (and stride between them) */
	uint32_t sequence;   /* FILLED: frame counter since STREAMON */
	uint32_t bytesused;  /* FILLED */
	uint32_t flags;      /* reserved, 0 */
	int64_t  t_mono_ns;  /* FILLED: capture time, CLOCK_MONOTONIC */
};

_Static_assert(sizeof(struct fwt_cam_msg) == 56, "fwt_cam_msg must stay 56 bytes");
_Static_assert(offsetof(struct fwt_cam_msg, t_mono_ns) == 48, "no padding before t_mono_ns");

static inline void fwt_cam_msg_init(struct fwt_cam_msg *m, uint16_t type)
{
	*m = (struct fwt_cam_msg){ .magic = FWT_CAM_MAGIC, .version = FWT_CAM_VERSION,
				   .type = type };
}

/* NV12 bytes for w x h (stride = w), and the buffer size the emulator uses
 * (rounded up to 4 KiB so every buffer starts page aligned). */
static inline uint32_t fwt_cam_nv12_size(uint32_t w, uint32_t h)
{
	return w * h * 3 / 2;
}

static inline uint32_t fwt_cam_buf_size(uint32_t w, uint32_t h)
{
	return (fwt_cam_nv12_size(w, h) + 4095u) & ~4095u;
}

const char *fwt_cam_type_str(uint16_t type);

/*
 * Send one message, with pass_fd attached (SCM_RIGHTS) if >= 0.
 * Returns 0 or -errno (-EAGAIN on a full non-blocking socket). Never SIGPIPE.
 */
int fwt_cam_send(int sock, const struct fwt_cam_msg *m, int pass_fd);

/*
 * Receive one message. If fd_out is given it gets an attached fd or -1;
 * without fd_out an attached fd is closed. Returns 1 message, 0 peer closed,
 * -EBADMSG not a message of ours (wrong size, magic or version), other -errno.
 */
int fwt_cam_recv(int sock, struct fwt_cam_msg *m, int *fd_out);

#endif
