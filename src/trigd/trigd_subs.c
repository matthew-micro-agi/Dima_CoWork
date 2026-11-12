#include "trigd_subs.h"

#include <string.h>

void trigd_subs_init(struct trigd_subs *l)
{
	memset(l, 0, sizeof(*l));
}

static int find(const struct trigd_subs *l, const struct sockaddr_un *a, socklen_t len)
{
	for (int i = 0; i < l->n; i++)
		if (l->s[i].len == len && memcmp(&l->s[i].addr, a, len) == 0)
			return i;
	return -1;
}

int trigd_subs_add(struct trigd_subs *l, const struct sockaddr_un *a, socklen_t len)
{
	if (len > sizeof(*a))
		return -1;
	if (find(l, a, len) >= 0)
		return 0;
	if (l->n >= TRIGD_MAX_SUBS)
		return -1;

	struct trigd_sub *s = &l->s[l->n++];

	memset(s, 0, sizeof(*s));
	memcpy(&s->addr, a, len);
	s->len = len;
	return 1;
}

bool trigd_subs_remove(struct trigd_subs *l, const struct sockaddr_un *a, socklen_t len)
{
	int i = find(l, a, len);

	if (i < 0)
		return false;
	l->s[i] = l->s[--l->n];  /* order doesn't matter */
	return true;
}
