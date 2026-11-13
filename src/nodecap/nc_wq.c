#include "nc_wq.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

int nc_wq_init(struct nc_wq *q, size_t cap_bytes)
{
	memset(q, 0, sizeof(*q));
	q->cap = cap_bytes;
	if (pthread_mutex_init(&q->m, NULL) != 0)
		return -EINVAL;
	if (pthread_cond_init(&q->c, NULL) != 0) {
		pthread_mutex_destroy(&q->m);
		return -EINVAL;
	}
	return 0;
}

void nc_wq_destroy(struct nc_wq *q)
{
	struct nc_wq_item *it = q->head;

	while (it) {
		struct nc_wq_item *next = it->next;

		free(it);
		it = next;
	}
	q->head = q->tail = NULL;
	pthread_cond_destroy(&q->c);
	pthread_mutex_destroy(&q->m);
}

bool nc_wq_push(struct nc_wq *q, const struct iovec *iov, int iovcnt, int64_t index, int64_t t_ns,
		bool key)
{
	size_t len = 0;

	for (int i = 0; i < iovcnt; i++)
		len += iov[i].iov_len;

	pthread_mutex_lock(&q->m);
	bool fits = !q->closed && q->bytes + len <= q->cap;

	if (!fits && !q->closed)
		q->overflow++;
	pthread_mutex_unlock(&q->m);
	if (!fits)
		return false;

	/* copy outside the lock; the writer only ever takes from the head */
	struct nc_wq_item *it = malloc(sizeof(*it) + len);

	if (!it) {
		pthread_mutex_lock(&q->m);
		q->overflow++;
		pthread_mutex_unlock(&q->m);
		return false;
	}
	*it = (struct nc_wq_item){ .len = len, .index = index, .t_ns = t_ns, .key = key };
	for (int i = 0, off = 0; i < iovcnt; off += (int)iov[i].iov_len, i++)
		memcpy(it->data + off, iov[i].iov_base, iov[i].iov_len);

	pthread_mutex_lock(&q->m);
	if (q->tail)
		q->tail->next = it;
	else
		q->head = it;
	q->tail = it;
	q->bytes += len;
	q->pushed++;
	if (q->bytes > q->peak_bytes)
		q->peak_bytes = q->bytes;
	pthread_cond_signal(&q->c);
	pthread_mutex_unlock(&q->m);
	return true;
}

struct nc_wq_item *nc_wq_pop(struct nc_wq *q)
{
	struct nc_wq_item *it;

	pthread_mutex_lock(&q->m);
	while (!q->head && !q->closed)
		pthread_cond_wait(&q->c, &q->m);
	it = q->head;
	if (it) {
		q->head = it->next;
		if (!q->head)
			q->tail = NULL;
		q->bytes -= it->len;
		it->next = NULL;
	}
	pthread_mutex_unlock(&q->m);
	return it;
}

void nc_wq_close(struct nc_wq *q)
{
	pthread_mutex_lock(&q->m);
	q->closed = true;
	pthread_cond_broadcast(&q->c);
	pthread_mutex_unlock(&q->m);
}
