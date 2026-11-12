#include "fwt_seqcheck.h"

#include <string.h>

void fwt_seqcheck_init(struct fwt_seqcheck *c)
{
	memset(c, 0, sizeof(*c));
}

enum fwt_seq_result fwt_seqcheck_feed(struct fwt_seqcheck *c, uint32_t session,
				      int64_t index, int64_t *missing)
{
	enum fwt_seq_result r;

	c->received++;

	if (!c->have) {
		r = FWT_SEQ_FIRST;
	} else if (session != c->session) {
		r = FWT_SEQ_NEW_SESSION;
	} else if (index == c->last_index + 1) {
		r = FWT_SEQ_OK;
	} else if (index > c->last_index + 1) {
		int64_t n = index - c->last_index - 1;

		c->gaps++;
		c->missing += n;
		if (missing)
			*missing = n;
		r = FWT_SEQ_GAP;
	} else if (index == c->last_index) {
		c->dups++;
		return FWT_SEQ_DUP;  /* keep last_index */
	} else {
		c->backward++;
		return FWT_SEQ_BACKWARD;  /* keep last_index: don't let a stray rewind the check */
	}

	c->have = true;
	c->session = session;
	c->last_index = index;
	return r;
}

const char *fwt_seq_result_str(enum fwt_seq_result r)
{
	switch (r) {
	case FWT_SEQ_FIRST:       return "first";
	case FWT_SEQ_OK:          return "ok";
	case FWT_SEQ_GAP:         return "gap";
	case FWT_SEQ_DUP:         return "duplicate";
	case FWT_SEQ_BACKWARD:    return "backward";
	case FWT_SEQ_NEW_SESSION: return "new-session";
	}
	return "?";
}
