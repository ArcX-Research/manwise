#include "manwise.h"
#include <stdlib.h>
#include <string.h>

static int alnum_ascii(unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}
int mw_topic(const char *s) {
    if (!s || !*s || strlen(s) > 128 || !alnum_ascii((unsigned char)*s)) return 0;
    for (; *s; s++) if (!alnum_ascii((unsigned char)*s) && !strchr("_.+-", *s)) return 0;
    return 1;
}
int mw_section(const char *s) {
    if (!s || !*s || strlen(s) > 16 || !((*s >= '1' && *s <= '9') || *s == 'n' || *s == 'l')) return 0;
    for (s++; *s; s++) if (!alnum_ascii((unsigned char)*s)) return 0;
    return 1;
}

static int clean_append(mw_buffer *b, size_t *column, const char *text, size_t size) {
    if (size > 2048 - *column) {
        if (!mw_append(b, "\n", 1)) return 0;
        *column = 0;
    }
    if (!mw_append(b, text, size)) return 0;
    *column += size;
    return 1;
}
char *mw_clean(const char *data, size_t size) {
    mw_buffer b = {.limit = MW_CAPTURE * 2U};
    size_t column = 0;
    for (size_t i = 0; i < size;) {
        unsigned char c = (unsigned char)data[i++];
        if (c == 27) {
            if (i < size && data[i] == '[') {
                i++;
                while (i < size) { c = (unsigned char)data[i++]; if (c >= 0x40 && c <= 0x7e) break; }
            } else if (i < size && (data[i] == ']' || data[i] == 'P' || data[i] == '^' || data[i] == '_')) {
                i++;
                while (i < size) {
                    if (data[i++] == '\a') break;
                    if (i >= 2 && data[i-2] == 27 && data[i-1] == '\\') break;
                }
            } else if (i < size) i++;
            continue;
        }
        if (c == '\b') {
            if (b.size && b.data[b.size-1] != '\n') {
                size_t before = b.size;
                do { b.size--; } while (b.size && ((unsigned char)b.data[b.size] & 0xc0U) == 0x80U);
                column -= before - b.size;
                b.data[b.size] = '\0';
            }
            continue;
        }
        if (c == '\n' || c == '\f') {
            if (!mw_append(&b, "\n", 1)) goto limit;
            column = 0; continue;
        }
        if (c < 32 && c != '\t') continue;
        if (c == 127) continue;
        if (c == '\t') {
            size_t spaces = 8 - column % 8;
            if (!clean_append(&b, &column, "        ", spaces)) goto limit;
            continue;
        }
        size_t bytes = 1;
        if (c >= 0xc2 && c <= 0xdf) bytes = 2;
        else if (c >= 0xe0 && c <= 0xef) bytes = 3;
        else if (c >= 0xf0 && c <= 0xf4) bytes = 4;
        if (bytes > size - (i - 1) || !mw_utf8(data + i - 1, bytes)) {
            if (!clean_append(&b, &column, "?", 1)) goto limit;
            continue;
        }
        /* Suppress C1 and bidirectional formatting controls in agent output. */
        if ((c == 0xc2 && (unsigned char)data[i] >= 0x80 && (unsigned char)data[i] <= 0x9f) ||
            (bytes == 3 && c == 0xe2 && (unsigned char)data[i] == 0x80 &&
             (unsigned char)data[i+1] >= 0xaa && (unsigned char)data[i+1] <= 0xae) ||
            (bytes == 3 && c == 0xe2 && (unsigned char)data[i] == 0x81 &&
             (unsigned char)data[i+1] >= 0xa6 && (unsigned char)data[i+1] <= 0xa9)) {
            i += bytes - 1; continue;
        }
        if (!clean_append(&b, &column, data + i - 1, bytes)) goto limit;
        i += bytes - 1;
    }
    if (!b.data) return mw_copy("");
    return b.data;
limit:
    free(b.data); return NULL;
}

static size_t line_count(const char *s) {
    size_t count = 0;
    for (; *s; s++) if (*s == '\n' || !s[1]) count++;
    return count;
}
cJSON *mw_excerpt(const char *text, size_t start, size_t count) {
    size_t total = line_count(text), line = 1, taken = 0;
    const char *p = text;
    while (*p && line < start) { if (*p++ == '\n') line++; }
    mw_buffer b = {.limit = MW_EXCERPT};
    while (*p && taken < count) {
        const char *end = strchr(p, '\n');
        size_t n = end ? (size_t)(end - p) + 1 : strlen(p);
        if (!mw_append(&b, p, n)) break;
        p += n; taken++;
    }
    cJSON *r = cJSON_CreateObject();
    mw_str(r, "text", b.data ? b.data : "");
    mw_num(r, "start_line", (double)start);
    mw_num(r, "end_line", (double)(taken ? start + taken - 1 : start - 1));
    mw_num(r, "total_lines", (double)total);
    mw_add(r, "next_start_line", *p ? cJSON_CreateNumber((double)(start + taken)) : cJSON_CreateNull());
    mw_buffer_free(&b);
    return r;
}
static unsigned char fold(unsigned char c) { return c >= 'A' && c <= 'Z' ? (unsigned char)(c + ('a' - 'A')) : c; }
static int contains(const char *s, size_t n, const char *q) {
    size_t m = strlen(q);
    for (size_t i = 0; i < n && m <= n - i; i++) {
        size_t j = 0;
        while (j < m && fold((unsigned char)s[i+j]) == fold((unsigned char)q[j])) j++;
        if (j == m) return 1;
    }
    return 0;
}
cJSON *mw_find(const char *text, const char *query, size_t start, size_t count, size_t context) {
    cJSON *r = cJSON_CreateObject(), *matches = cJSON_CreateArray();
    mw_add(r, "matches", matches);
    size_t line = 1, found = 0, bytes = 0;
    const char *p = text;
    for (; *p;) {
        const char *end = strchr(p, '\n');
        size_t n = end ? (size_t)(end - p) + 1 : strlen(p);
        if (line >= start && contains(p, n, query)) {
            if (found == count) break;
            size_t from = line > context ? line - context : 1;
            cJSON *match = mw_excerpt(text, from, line + context - from + 1);
            size_t length = strlen(mw_string(mw_get(match, "text")));
            if (length > MW_EXCERPT - bytes) { cJSON_Delete(match); break; }
            bytes += length; found++;
            mw_num(match, "line", (double)line);
            mw_push(matches, match);
        }
        p += n; line++;
    }
    mw_num(r, "total_lines", (double)line_count(text));
    mw_add(r, "next_start_line", *p ? cJSON_CreateNumber((double)line) : cJSON_CreateNull());
    return r;
}
