#include "ctl_http.h"

#include <curl/curl.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define CALL_TIMEOUT_S 10L

void ctl_buf_free(struct ctl_buf *b)
{
	free(b->data);
	*b = (struct ctl_buf){ 0 };
}

static size_t to_buf(char *p, size_t size, size_t n, void *user)
{
	struct ctl_buf *b = user;
	size_t len = size * n;

	if (b->len + len > b->cap) {
		size_t cap = b->cap ? b->cap : 4096;

		while (cap < b->len + len)
			cap *= 2;
		uint8_t *d = realloc(b->data, cap);

		if (!d)
			return 0;  /* curl reports a write error */
		b->data = d;
		b->cap = cap;
	}
	memcpy(b->data + b->len, p, len);
	b->len += len;
	return len;
}

int ctl_http_init(struct ctl_http *h, const char *addr)
{
	*h = (struct ctl_http){ 0 };
	if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
		return -1;
	h->curl = curl_easy_init();
	if (!h->curl)
		return -1;
	snprintf(h->base, sizeof(h->base), "http://%s%s", addr, strchr(addr, ':') ? "" : ":8080");
	return 0;
}

void ctl_http_free(struct ctl_http *h)
{
	if (h->curl)
		curl_easy_cleanup(h->curl);
	h->curl = NULL;
	curl_global_cleanup();
}

static void setup(struct ctl_http *h, const char *path, char *errbuf)
{
	char url[512];

	snprintf(url, sizeof(url), "%s%s", h->base, path);
	curl_easy_reset(h->curl);
	curl_easy_setopt(h->curl, CURLOPT_URL, url);
	curl_easy_setopt(h->curl, CURLOPT_ERRORBUFFER, errbuf);
	curl_easy_setopt(h->curl, CURLOPT_CONNECTTIMEOUT, 5L);
	curl_easy_setopt(h->curl, CURLOPT_NOSIGNAL, 1L);
	errbuf[0] = '\0';
}

static int finish(struct ctl_http *h, CURLcode rc, const char *errbuf, long *status)
{
	if (rc != CURLE_OK) {
		snprintf(h->err, sizeof(h->err), "%s", errbuf[0] ? errbuf : curl_easy_strerror(rc));
		return -1;
	}
	curl_easy_getinfo(h->curl, CURLINFO_RESPONSE_CODE, status);
	return 0;
}

int ctl_http_call(struct ctl_http *h, const char *path, const uint8_t *body, size_t body_len,
		  bool post, struct ctl_buf *reply, long *status)
{
	char errbuf[CURL_ERROR_SIZE];
	struct curl_slist *hdr = NULL;

	reply->len = 0;
	setup(h, path, errbuf);
	curl_easy_setopt(h->curl, CURLOPT_TIMEOUT, CALL_TIMEOUT_S);
	curl_easy_setopt(h->curl, CURLOPT_WRITEFUNCTION, to_buf);
	curl_easy_setopt(h->curl, CURLOPT_WRITEDATA, reply);
	if (post) {
		hdr = curl_slist_append(hdr, "Content-Type: application/cbor");
		curl_easy_setopt(h->curl, CURLOPT_HTTPHEADER, hdr);
		curl_easy_setopt(h->curl, CURLOPT_POST, 1L);
		curl_easy_setopt(h->curl, CURLOPT_POSTFIELDS, body ? (const char *)body : "");
		curl_easy_setopt(h->curl, CURLOPT_POSTFIELDSIZE, (long)body_len);
	}

	CURLcode rc = curl_easy_perform(h->curl);

	curl_slist_free_all(hdr);
	return finish(h, rc, errbuf, status);
}

/* ---- fetch: body streamed to a file, X-Fwt-* headers parsed ---- */

struct fetch_ctx {
	struct ctl_http *h;
	FILE *out;
	struct ctl_fetch_info *info;
	struct ctl_buf *err_reply;
	long status;  /* from the status line, before the body */
};

static size_t on_header(char *p, size_t size, size_t n, void *user)
{
	struct fetch_ctx *c = user;
	size_t len = size * n;
	char line[700];
	size_t k = len < sizeof(line) - 1 ? len : sizeof(line) - 1;

	memcpy(line, p, k);
	line[k] = '\0';
	line[strcspn(line, "\r\n")] = '\0';

	if (!strncmp(line, "HTTP/", 5)) {
		const char *sp = strchr(line, ' ');

		c->status = sp ? strtol(sp + 1, NULL, 10) : 0;
		return len;
	}
	char *colon = strchr(line, ':');

	if (!colon)
		return len;
	*colon = '\0';

	const char *v = colon + 1;

	while (*v == ' ')
		v++;
	if (!strcasecmp(line, "X-Fwt-Session"))
		c->info->session = (uint32_t)strtoul(v, NULL, 10);
	else if (!strcasecmp(line, "X-Fwt-From"))
		c->info->from = strtoll(v, NULL, 10);
	else if (!strcasecmp(line, "X-Fwt-To"))
		c->info->to = strtoll(v, NULL, 10);
	else if (!strcasecmp(line, "X-Fwt-Missing"))
		snprintf(c->info->missing, sizeof(c->info->missing), "%s", v);
	else if (!strcasecmp(line, "X-Fwt-Missing-Inside"))
		c->info->missing_inside = strtoull(v, NULL, 10);
	else if (!strcasecmp(line, "X-Fwt-Open-Segment"))
		snprintf(c->info->open_segment, sizeof(c->info->open_segment), "%s", v);
	return len;
}

static size_t on_body(char *p, size_t size, size_t n, void *user)
{
	struct fetch_ctx *c = user;
	size_t len = size * n;

	if (c->status != 200)  /* an error reply: CBOR, keep it */
		return to_buf(p, size, n, c->err_reply);
	if (fwrite(p, 1, len, c->out) != len)
		return 0;
	c->info->bytes += len;
	return len;
}

int ctl_http_fetch(struct ctl_http *h, const char *path, FILE *out, struct ctl_fetch_info *info,
		   struct ctl_buf *err_reply, long *status)
{
	char errbuf[CURL_ERROR_SIZE];
	struct fetch_ctx c = { .h = h, .out = out, .info = info, .err_reply = err_reply };

	*info = (struct ctl_fetch_info){ .from = -1, .to = -1 };
	err_reply->len = 0;
	setup(h, path, errbuf);
	/* no overall timeout (10 min of video may take a while); stalls are caught */
	curl_easy_setopt(h->curl, CURLOPT_LOW_SPEED_LIMIT, 1024L);
	curl_easy_setopt(h->curl, CURLOPT_LOW_SPEED_TIME, 30L);
	curl_easy_setopt(h->curl, CURLOPT_HEADERFUNCTION, on_header);
	curl_easy_setopt(h->curl, CURLOPT_HEADERDATA, &c);
	curl_easy_setopt(h->curl, CURLOPT_WRITEFUNCTION, on_body);
	curl_easy_setopt(h->curl, CURLOPT_WRITEDATA, &c);
	return finish(h, curl_easy_perform(h->curl), errbuf, status);
}
