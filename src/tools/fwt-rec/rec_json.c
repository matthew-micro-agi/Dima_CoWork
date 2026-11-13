#include "rec_json.h"

#include <inttypes.h>

void rec_json_str(FILE *f, const char *s)
{
	fputc('"', f);
	for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
		switch (*p) {
		case '"':  fputs("\\\"", f); break;
		case '\\': fputs("\\\\", f); break;
		case '\n': fputs("\\n", f); break;
		case '\r': fputs("\\r", f); break;
		case '\t': fputs("\\t", f); break;
		default:
			if (*p < 0x20)
				fprintf(f, "\\u%04x", *p);
			else
				fputc(*p, f);
		}
	}
	fputc('"', f);
}

static void key(FILE *f, int indent, const char *k)
{
	fprintf(f, "%*s", indent, "");
	rec_json_str(f, k);
	fputs(": ", f);
}

void rec_json_write(FILE *f, const struct rec_result *r)
{
	fputs("{\n", f);
	key(f, 2, "tool"); fputs("\"fwt-rec\",\n", f);
	key(f, 2, "tool_version"); rec_json_str(f, r->tool_version); fputs(",\n", f);
	key(f, 2, "started_utc"); rec_json_str(f, r->started_utc); fputs(",\n", f);

	key(f, 2, "node"); fputs("{\n", f);
	key(f, 4, "addr"); rec_json_str(f, r->addr); fputs(",\n", f);
	key(f, 4, "name"); rec_json_str(f, r->id.node); fputs(",\n", f);
	key(f, 4, "version"); rec_json_str(f, r->id.version); fputs(",\n", f);
	key(f, 4, "width"); fprintf(f, "%" PRIu32 ",\n", r->id.width);
	key(f, 4, "height"); fprintf(f, "%" PRIu32 "\n", r->id.height);
	fputs("  },\n", f);

	key(f, 2, "plan"); fputs("{\n", f);
	key(f, 4, "session"); fprintf(f, "%" PRIu32 ",\n", r->session);
	key(f, 4, "fps"); fprintf(f, "%" PRIu32 ",\n", r->fps);
	key(f, 4, "t0_ns"); fprintf(f, "%" PRId64 ",\n", r->arm.t0);
	key(f, 4, "period_ns"); fprintf(f, "%" PRIu64 ",\n", r->arm.period);
	key(f, 4, "requested_s"); fprintf(f, "%g,\n", r->requested_s);
	key(f, 4, "interrupted"); fputs(r->interrupted ? "true\n" : "false\n", f);
	fputs("  },\n", f);

	key(f, 2, "recorded"); fputs("{\n", f);
	key(f, 4, "status"); rec_json_str(f, r->rec.status); fputs(",\n", f);
	key(f, 4, "first_idx"); fprintf(f, "%" PRId64 ",\n", r->rec.first_idx);
	key(f, 4, "last_idx"); fprintf(f, "%" PRId64 ",\n", r->rec.last_idx);
	key(f, 4, "frames"); fprintf(f, "%" PRIu64 ",\n", r->rec.frames);
	key(f, 4, "missing"); fprintf(f, "%" PRIu64 ",\n", r->rec.missing);
	key(f, 4, "truncated"); fputs(r->rec.truncated ? "true\n" : "false\n", f);
	fputs("  },\n", f);

	key(f, 2, "file"); fputs("{\n", f);
	key(f, 4, "path"); rec_json_str(f, r->out_path); fputs(",\n", f);
	key(f, 4, "bytes"); fprintf(f, "%" PRIu64 ",\n", r->file_bytes);
	key(f, 4, "frames"); fprintf(f, "%" PRIu64 ",\n", r->file_frames);
	key(f, 4, "fetches"); fprintf(f, "%u,\n", r->fetches);
	key(f, 4, "missing_ranges"); rec_json_str(f, r->missing_ranges); fputs(",\n", f);
	key(f, 4, "missing_inside"); fprintf(f, "%" PRIu64 "\n", r->missing_inside);
	fputs("  },\n", f);

	key(f, 2, "check"); fputs("{\n", f);
	key(f, 4, "tool"); fputs("\"sei-dump\",\n", f);
	key(f, 4, "result"); rec_json_str(f, r->check); fputs(",\n", f);
	key(f, 4, "summary"); rec_json_str(f, r->check_summary); fputs("\n", f);
	fputs("  }\n", f);
	fputs("}\n", f);
}
