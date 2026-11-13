/* Unit tests for nodecap's write queue (storage decoupled from capture). */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nc_wq.h"

static int failures;

#define CHECK(cond)                                                          \
	do {                                                                 \
		if (!(cond)) {                                               \
			fprintf(stderr, "%s:%d: FAILED: %s\n", __FILE__,     \
				__LINE__, #cond);                            \
			failures++;                                          \
		}                                                            \
	} while (0)

static bool push(struct nc_wq *q, const char *a, const char *b, int64_t index)
{
	struct iovec v[2] = { { (void *)a, strlen(a) }, { (void *)b, strlen(b) } };

	return nc_wq_push(q, v, 2, index, index * 10, index % 2 == 0);
}

/* order, contents (iov joined), byte limit, overflow counted, drain after close */
static void test_basic(void)
{
	struct nc_wq q;
	struct nc_wq_item *it;

	CHECK(nc_wq_init(&q, 20) == 0);
	CHECK(push(&q, "abc", "de", 0));       /* 5 */
	CHECK(push(&q, "fghij", "klmno", 1));  /* 10 -> 15 */
	CHECK(!push(&q, "pqrst", "u", 2));     /* 6 -> 21 > 20: overflow */
	CHECK(push(&q, "vw", "xyz", 3));       /* 5 -> 20 */
	CHECK(q.overflow == 1 && q.pushed == 3 && q.bytes == 20 && q.peak_bytes == 20);

	it = nc_wq_pop(&q);
	CHECK(it && it->len == 5 && !memcmp(it->data, "abcde", 5) && it->index == 0 && it->key &&
	      it->t_ns == 0);
	free(it);
	CHECK(q.bytes == 15);
	CHECK(push(&q, "12345", "", 4));        /* room again */

	nc_wq_close(&q);
	CHECK(!push(&q, "x", "", 5) && q.overflow == 1);  /* closed: refused, not an overflow */
	int64_t want[] = { 1, 3, 4 };

	for (int k = 0; k < 3; k++) {
		it = nc_wq_pop(&q);
		CHECK(it && it->index == want[k]);
		free(it);
	}
	CHECK(nc_wq_pop(&q) == NULL && q.bytes == 0);
	nc_wq_destroy(&q);
}

/* a slow consumer never blocks the producer: it overflows instead */
struct slow {
	struct nc_wq *q;
	int got;
};

static void *consumer(void *arg)
{
	struct slow *s = arg;
	struct nc_wq_item *it;

	while ((it = nc_wq_pop(s->q))) {
		s->got++;
		free(it);
	}
	return NULL;
}

static void test_threads(void)
{
	struct nc_wq q;
	struct slow s = { .q = &q };
	pthread_t th;
	char frame[1000];
	struct iovec v = { frame, sizeof(frame) };
	int ok = 0;

	memset(frame, 7, sizeof(frame));
	CHECK(nc_wq_init(&q, 64 * 1000) == 0);
	CHECK(pthread_create(&th, NULL, consumer, &s) == 0);
	for (int k = 0; k < 10000; k++)
		ok += nc_wq_push(&q, &v, 1, k, k, false);
	nc_wq_close(&q);
	pthread_join(th, NULL);
	CHECK(s.got == ok && (uint64_t)(10000 - ok) == q.overflow);
	CHECK(q.peak_bytes <= 64 * 1000);
	nc_wq_destroy(&q);
}

int main(void)
{
	test_basic();
	test_threads();
	if (failures) {
		fprintf(stderr, "test_wq: %d failure(s)\n", failures);
		return 1;
	}
	printf("test_wq: all passed\n");
	return 0;
}
