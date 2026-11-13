#include "fwt_cam_proto.h"

#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

const char *fwt_cam_type_str(uint16_t type)
{
	switch (type) {
	case FWT_CAM_FORMAT:    return "FORMAT";
	case FWT_CAM_REQBUFS:   return "REQBUFS";
	case FWT_CAM_QBUF:      return "QBUF";
	case FWT_CAM_STREAMON:  return "STREAMON";
	case FWT_CAM_STREAMOFF: return "STREAMOFF";
	case FWT_CAM_FILLED:    return "FILLED";
	case FWT_CAM_ERROR:     return "ERROR";
	default:                return "?";
	}
}

int fwt_cam_send(int sock, const struct fwt_cam_msg *m, int pass_fd)
{
	union {
		struct cmsghdr h;
		char buf[CMSG_SPACE(sizeof(int))];
	} c;
	struct iovec iov = { .iov_base = (void *)m, .iov_len = sizeof(*m) };
	struct msghdr mh = { .msg_iov = &iov, .msg_iovlen = 1 };

	if (pass_fd >= 0) {
		memset(&c, 0, sizeof(c));
		mh.msg_control = c.buf;
		mh.msg_controllen = sizeof(c.buf);
		struct cmsghdr *ch = CMSG_FIRSTHDR(&mh);

		ch->cmsg_level = SOL_SOCKET;
		ch->cmsg_type = SCM_RIGHTS;
		ch->cmsg_len = CMSG_LEN(sizeof(int));
		memcpy(CMSG_DATA(ch), &pass_fd, sizeof(int));
	}
	ssize_t n = sendmsg(sock, &mh, MSG_NOSIGNAL);

	if (n < 0)
		return -errno;
	return n == (ssize_t)sizeof(*m) ? 0 : -EMSGSIZE;
}

int fwt_cam_recv(int sock, struct fwt_cam_msg *m, int *fd_out)
{
	union {
		struct cmsghdr h;
		char buf[CMSG_SPACE(sizeof(int))];
	} c;
	struct iovec iov = { .iov_base = m, .iov_len = sizeof(*m) };
	struct msghdr mh = { .msg_iov = &iov, .msg_iovlen = 1,
			     .msg_control = c.buf, .msg_controllen = sizeof(c.buf) };
	int fd = -1;

	if (fd_out)
		*fd_out = -1;
	ssize_t n = recvmsg(sock, &mh, MSG_CMSG_CLOEXEC);

	if (n < 0)
		return -errno;
	if (n == 0)
		return 0;
	for (struct cmsghdr *ch = CMSG_FIRSTHDR(&mh); ch; ch = CMSG_NXTHDR(&mh, ch))
		if (ch->cmsg_level == SOL_SOCKET && ch->cmsg_type == SCM_RIGHTS &&
		    ch->cmsg_len == CMSG_LEN(sizeof(int)))
			memcpy(&fd, CMSG_DATA(ch), sizeof(int));

	bool ok = n == (ssize_t)sizeof(*m) && !(mh.msg_flags & (MSG_TRUNC | MSG_CTRUNC)) &&
		  m->magic == FWT_CAM_MAGIC && m->version == FWT_CAM_VERSION;

	if (!ok || !fd_out) {
		if (fd >= 0)
			close(fd);
		fd = -1;
	}
	if (fd_out)
		*fd_out = fd;
	return ok ? 1 : -EBADMSG;
}
