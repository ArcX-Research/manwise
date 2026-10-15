#include "manwise.h"
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

_Noreturn void mw_fatal(const char *message) { fprintf(stderr, "manwise: %s\n", message); exit(2); }
void *mw_alloc(size_t size) {
    void *p = calloc(1, size ? size : 1);
    if (!p) mw_fatal("out of memory");
    return p;
}
char *mw_copy(const char *text) {
    size_t n = strlen(text) + 1;
    char *p = mw_alloc(n);
    memcpy(p, text, n);
    return p;
}
double mw_now(void) {
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t)) mw_fatal("monotonic clock unavailable");
    return (double)t.tv_sec * 1000.0 + (double)t.tv_nsec / 1000000.0;
}
int mw_append(mw_buffer *b, const char *data, size_t n) {
    if (n > b->limit - b->size) return 0;
    size_t needed = b->size + n + 1;
    if (needed > b->capacity) {
        size_t capacity = b->capacity ? b->capacity : 1024;
        while (capacity < needed && capacity < b->limit + 1) {
            capacity = capacity > (b->limit + 1) / 2 ? b->limit + 1 : capacity * 2;
        }
        char *p = realloc(b->data, capacity);
        if (!p) mw_fatal("out of memory");
        b->data = p; b->capacity = capacity;
    }
    if (n) memcpy(b->data + b->size, data, n);
    b->size += n; b->data[b->size] = '\0';
    return 1;
}
void mw_buffer_free(mw_buffer *b) { free(b->data); memset(b, 0, sizeof(*b)); }
const cJSON *mw_get(const cJSON *object, const char *key) { return cJSON_GetObjectItemCaseSensitive(object, key); }
const char *mw_string(const cJSON *v) { return cJSON_IsString(v) ? v->valuestring : NULL; }
void mw_add(cJSON *o, const char *key, cJSON *v) {
    if (!v || !cJSON_AddItemToObject(o, key, v)) { cJSON_Delete(v); mw_fatal("out of memory"); }
}
void mw_push(cJSON *a, cJSON *v) {
    if (!v || !cJSON_AddItemToArray(a, v)) { cJSON_Delete(v); mw_fatal("out of memory"); }
}
void mw_str(cJSON *o, const char *k, const char *v) { mw_add(o, k, cJSON_CreateString(v)); }
void mw_num(cJSON *o, const char *k, double v) { mw_add(o, k, cJSON_CreateNumber(v)); }
int mw_keys(const cJSON *o, const char *const *keys) {
    if (!cJSON_IsObject(o)) return 0;
    for (const cJSON *v = o->child; v; v = v->next) {
        size_t i = 0;
        while (keys[i] && strcmp(keys[i], v->string)) i++;
        if (!keys[i]) return 0;
    }
    return 1;
}
int mw_integer(const cJSON *v, long long min, long long max, long long *out) {
    if (!v || !cJSON_IsNumber(v) || !v->valuestring) return 0;
    const char *p = v->valuestring;
    if (*p == '-') p++;
    if (!*p) return 0;
    for (; *p; p++) if (*p < '0' || *p > '9') return 0;
    errno = 0; char *end = NULL;
    long long result = strtoll(v->valuestring, &end, 10);
    if (errno || *end || result < min || result > max) return 0;
    *out = result; return 1;
}

int mw_utf8(const char *data, size_t size) {
    const unsigned char *s = (const unsigned char *)data;
    for (size_t i = 0; i < size;) {
        unsigned c = s[i++], value; size_t extra;
        if (c < 0x80) continue;
        if (c >= 0xc2 && c <= 0xdf) { extra = 1; value = c & 31U; }
        else if (c >= 0xe0 && c <= 0xef) { extra = 2; value = c & 15U; }
        else if (c >= 0xf0 && c <= 0xf4) { extra = 3; value = c & 7U; }
        else return 0;
        if (size - i < extra) return 0;
        for (size_t j = 0; j < extra; j++) {
            unsigned next = s[i++];
            if ((next & 0xc0U) != 0x80U) return 0;
            value = (value << 6) | (next & 63U);
        }
        if ((extra == 1 && value < 0x80) || (extra == 2 && value < 0x800) ||
            (extra == 3 && value < 0x10000) || value > 0x10ffff ||
            (value >= 0xd800 && value <= 0xdfff)) return 0;
    }
    return 1;
}

/* Check lexical forms cJSON accepts permissively, before allocating a tree. */
typedef struct { size_t start, size; } number_span;
static int lexical(const char *s, size_t n, number_span *numbers, size_t *count) {
    size_t depth = 0;
    for (size_t i = 0; i < n;) {
        unsigned char c = (unsigned char)s[i++];
        if (c == '"') {
            int closed = 0;
            while (i < n) {
                c = (unsigned char)s[i++];
                if (c == '"') { closed = 1; break; }
                if (c < 32) return 0;
                if (c == '\\') {
                    if (i == n) return 0;
                    c = (unsigned char)s[i++];
                    if (c == 'u') {
                        unsigned value = 0;
                        if (n - i < 4) return 0;
                        for (size_t k = 0; k < 4; k++) {
                            c = (unsigned char)s[i++];
                            unsigned digit;
                            if (c >= '0' && c <= '9') digit = c - '0';
                            else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10U;
                            else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10U;
                            else return 0;
                            value = value * 16 + digit;
                        }
                        if (!value) return 0;
                    } else if (!strchr("\"\\/bfnrt", (int)c)) return 0;
                }
            }
            if (!closed) return 0;
        } else if (c == '{' || c == '[') {
            if (++depth > 32) return 0;
        } else if (c == '}' || c == ']') {
            if (!depth) return 0;
            depth--;
        } else if (c == '-' || (c >= '0' && c <= '9')) {
            size_t begin = i - 1;
            if (c == '-') { if (i == n) return 0; c = (unsigned char)s[i++]; }
            if (c == '0') { if (i < n && s[i] >= '0' && s[i] <= '9') return 0; }
            else if (c >= '1' && c <= '9') { while (i < n && s[i] >= '0' && s[i] <= '9') i++; }
            else return 0;
            if (i < n && s[i] == '.') {
                i++; size_t start = i;
                while (i < n && s[i] >= '0' && s[i] <= '9') i++;
                if (start == i) return 0;
            }
            if (i < n && (s[i] == 'e' || s[i] == 'E')) {
                i++; if (i < n && (s[i] == '+' || s[i] == '-')) i++;
                size_t start = i;
                while (i < n && s[i] >= '0' && s[i] <= '9') i++;
                if (start == i) return 0;
            }
            if (i - begin > 64) return 0;
            if (i < n && !strchr(" \t\r\n,}]", s[i])) return 0;
            if (*count == 2048) return 0;
            numbers[*count].start = begin; numbers[*count].size = i - begin; (*count)++;
        } else if (c < 32 && c != '\t' && c != '\r' && c != '\n') return 0;
    }
    return depth == 0;
}
static int tree_valid(cJSON *v, size_t *nodes, const char *s, const number_span *numbers, size_t count, size_t *at) {
    if (++*nodes > 2048) return 0;
    if (cJSON_IsNumber(v)) {
        if (!isfinite(v->valuedouble) || *at >= count) return 0;
        /* Preserve input spelling so integer validation never relies on a
         * rounded floating-point conversion. cJSON owns and frees this field. */
        number_span span = numbers[(*at)++];
        v->valuestring = mw_alloc(span.size + 1);
        memcpy(v->valuestring, s + span.start, span.size);
    }
    for (cJSON *a = v->child; a; a = a->next) {
        if (cJSON_IsObject(v)) {
            for (const cJSON *b = a->next; b; b = b->next)
                if (!strcmp(a->string, b->string)) return 0;
        }
        if (!tree_valid(a, nodes, s, numbers, count, at)) return 0;
    }
    return 1;
}
cJSON *mw_parse(const char *s, size_t n) {
    number_span numbers[2048]; size_t count = 0, at = 0;
    if (!s || !n || n > MW_INPUT || memchr(s, '\0', n) || !mw_utf8(s, n) || !lexical(s, n, numbers, &count)) return NULL;
    const char *end = NULL;
    cJSON *root = cJSON_ParseWithLengthOpts(s, n, &end, 0);
    if (!root) return NULL;
    while (end < s + n && strchr(" \t\r\n", *end)) end++;
    size_t nodes = 0;
    if (end != s + n || !tree_valid(root, &nodes, s, numbers, count, &at) || at != count) { cJSON_Delete(root); return NULL; }
    return root;
}
