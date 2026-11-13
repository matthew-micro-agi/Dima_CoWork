/*
 * Session control (DL-23): arm starts trigd + nodecap --rec for one session,
 * disarm stops them cleanly; a child that dies fails the session.
 */
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "agent.h"
#include "trigd_t0.h"

#define FD_TO_CLOCKID(fd) ((~(clockid_t)(fd) << 3) | 3)
#define MAX_ARGS 40

int64_t na_ptp_now(const struct agent *a)
{
	struct timespec ts;
	int fd = a->o.ptp_dev ? open(a->o.ptp_dev, O_RDONLY | O_CLOEXEC) : -1;
	int r = clock_gettime(fd >= 0 ? FD_TO_CLOCKID(fd) : CLOCK_REALTIME, &ts);

	if (fd >= 0)
		close(fd);
	return r == 0 ? (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec : 0;
}

/* Split extra args on spaces into argv (no quoting; trial options only). */
static int split(char *s, char **argv, int n, int max)
{
	for (char *tok = strtok(s, " "); tok && n < max - 1; tok = strtok(NULL, " "))
		argv[n++] = tok;
	argv[n] = NULL;
	return n;
}

static pid_t spawn(const struct agent *a, char **argv, const char *logname)
{
	char path[512];
	pid_t pid = fork();

	if (pid != 0)
		return pid;  /* parent (or -1) */

	/* child: own process group (Ctrl-C to nodeagent doesn't hit it), output to a log */
	setpgid(0, 0);
	snprintf(path, sizeof(path), "%s/%s", a->o.log_dir, logname);

	int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);

	if (fd >= 0) {
		dup2(fd, STDOUT_FILENO);
		dup2(fd, STDERR_FILENO);
	}
	execv(argv[0], argv);
	_exit(127);
}

static bool wait_exit(pid_t pid, int ms, int *status)
{
	for (int i = 0; i < ms / 20; i++) {
		pid_t r = waitpid(pid, status, WNOHANG);

		if (r == pid || (r < 0 && errno == ECHILD))
			return true;
		usleep(20000);
	}
	return false;
}

int na_arm(struct agent *a, uint32_t session, unsigned fps, bool t0_given, int64_t t0,
	   const char **code, char *msg, size_t msg_len)
{
	if (a->state == NA_RECORDING) {
		*code = "busy";
		snprintf(msg, msg_len, "session %" PRIu32 " is recording; disarm first",
			 a->session);
		return 409;
	}
	if (session == 0 || fps < 1 || fps > 60) {
		*code = "bad-request";
		snprintf(msg, msg_len, "need session > 0 and 1 <= fps <= 60");
		return 400;
	}
	na_index_refresh(a);
	if (a->idx_loaded && rec_find_session(&a->idx, session)) {
		*code = "session-exists";
		snprintf(msg, msg_len, "session %" PRIu32 " already on disk (DL-20)", session);
		return 409;
	}

	int64_t now = na_ptp_now(a), period = trigd_period_from_fps((int)fps);

	if (!t0_given)
		t0 = trigd_t0_auto(now, 1000000000LL);
	if (t0 < now + 300000000LL) {  /* children need time to start and subscribe */
		*code = "t0-too-soon";
		snprintf(msg, msg_len, "t0 must be >= now + 300 ms (now %" PRId64 ")", now);
		return 400;
	}

	char sess[16], fpss[8], t0s[24], ring[24];
	char targs[256], nargs[256];
	char *tv[MAX_ARGS], *nv[MAX_ARGS];
	int tn = 0, nn = 0;

	snprintf(sess, sizeof(sess), "%" PRIu32, session);
	snprintf(fpss, sizeof(fpss), "%u", fps);
	snprintf(t0s, sizeof(t0s), "%" PRId64, t0);
	snprintf(ring, sizeof(ring), "%" PRIu64, a->o.ring_max_mb);
	snprintf(targs, sizeof(targs), "%s", a->o.trigd_args);
	snprintf(nargs, sizeof(nargs), "%s", a->o.nodecap_args);

	tv[tn++] = (char *)a->o.trigd_bin;
	tv[tn++] = "--session"; tv[tn++] = sess;
	tv[tn++] = "--fps";     tv[tn++] = fpss;
	tv[tn++] = "--t0";      tv[tn++] = t0s;
	tv[tn++] = "--sock";    tv[tn++] = (char *)a->o.trigd_sock;
	split(targs, tv, tn, MAX_ARGS);

	nv[nn++] = (char *)a->o.nodecap_bin;
	nv[nn++] = "--rec";      nv[nn++] = (char *)a->o.rec_dir;
	nv[nn++] = "--sock";     nv[nn++] = (char *)a->o.trigd_sock;
	nv[nn++] = "--ctl-sock"; nv[nn++] = (char *)a->o.ctl_sock;
	if (a->o.ring_max_mb) {
		nv[nn++] = "--ring-max-mb";
		nv[nn++] = ring;
	}
	nv[nn++] = "-q";
	split(nargs, nv, nn, MAX_ARGS);

	/* trigd first: nodecap subscribes to its socket. */
	unlink(a->o.trigd_sock);
	a->trigd_pid = spawn(a, tv, "trigd.log");
	for (int i = 0; i < 50 && access(a->o.trigd_sock, F_OK) != 0; i++)
		usleep(20000);
	if (a->trigd_pid < 0 || access(a->o.trigd_sock, F_OK) != 0) {
		if (a->trigd_pid > 0) {
			kill(a->trigd_pid, SIGINT);
			wait_exit(a->trigd_pid, 2000, &(int){ 0 });
		}
		a->trigd_pid = 0;
		*code = "start-failed";
		snprintf(msg, msg_len, "trigd did not start (see %s/trigd.log)", a->o.log_dir);
		return 500;
	}
	a->nodecap_pid = spawn(a, nv, "nodecap.log");
	if (a->nodecap_pid < 0) {
		kill(a->trigd_pid, SIGINT);
		wait_exit(a->trigd_pid, 2000, &(int){ 0 });
		a->trigd_pid = 0;
		a->nodecap_pid = 0;
		*code = "start-failed";
		snprintf(msg, msg_len, "nodecap did not start");
		return 500;
	}
	a->state = NA_RECORDING;
	a->disarming = false;
	a->session = session;
	a->t0_ns = t0;
	a->period_ns = period;
	a->message[0] = 0;
	fprintf(stderr, "nodeagent: armed session %" PRIu32 ", T0 %" PRId64 ", %u fps (trigd %d, "
		"nodecap %d)\n", session, t0, fps, (int)a->trigd_pid, (int)a->nodecap_pid);
	return 0;
}

int na_disarm(struct agent *a, const char **code, char *msg, size_t msg_len)
{
	int st = 0;

	if (a->state != NA_RECORDING) {
		*code = "not-armed";
		snprintf(msg, msg_len, "no session is recording");
		return 409;
	}
	a->disarming = true;
	/* trigd stops, disarms its output and sends END; nodecap then closes the
	 * open segment, ends the session in the manifest and exits. */
	if (a->trigd_pid > 0) {
		kill(a->trigd_pid, SIGINT);
		if (!wait_exit(a->trigd_pid, 3000, &st))
			kill(a->trigd_pid, SIGKILL), wait_exit(a->trigd_pid, 1000, &st);
	}
	if (a->nodecap_pid > 0 && !wait_exit(a->nodecap_pid, 8000, &st)) {
		kill(a->nodecap_pid, SIGINT);
		if (!wait_exit(a->nodecap_pid, 3000, &st))
			kill(a->nodecap_pid, SIGKILL), wait_exit(a->nodecap_pid, 1000, &st);
	}
	snprintf(a->message, sizeof(a->message), "session %" PRIu32 " stopped; nodecap exit %d%s",
		 a->session, WIFEXITED(st) ? WEXITSTATUS(st) : -1,
		 WIFEXITED(st) && WEXITSTATUS(st) ? " (gaps or drops: see nodecap.log)" : "");
	a->trigd_pid = a->nodecap_pid = 0;
	a->state = NA_IDLE;
	a->disarming = false;
	fprintf(stderr, "nodeagent: %s\n", a->message);
	return 0;
}

void na_poll_children(struct agent *a)
{
	int st;

	if (a->state != NA_RECORDING || a->disarming)
		return;
	for (int k = 0; k < 2; k++) {
		pid_t *pid = k ? &a->nodecap_pid : &a->trigd_pid;

		if (*pid > 0 && waitpid(*pid, &st, WNOHANG) == *pid) {
			snprintf(a->message, sizeof(a->message), "%s exited during session %" PRIu32
				 " (status %d)", k ? "nodecap" : "trigd", a->session,
				 WIFEXITED(st) ? WEXITSTATUS(st) : -1);
			fprintf(stderr, "nodeagent: %s\n", a->message);
			*pid = 0;
			/* stop the other one too */
			pid_t *other = k ? &a->trigd_pid : &a->nodecap_pid;

			if (*other > 0) {
				kill(*other, SIGINT);
				wait_exit(*other, 8000, &st);
				*other = 0;
			}
			a->state = NA_FAILED;
			return;
		}
	}
}
