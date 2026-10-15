#include "manwise.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void parser(void) {
    const char *bad[] = {"", "{", "[}", "{}{}", "{\"x\":1,\"x\":2}", "{\"x\":01}",
        "{\"x\":1.}", "{\"x\":1e}", "{\"x\":1e999}", "{\"x\":NaN}",
        "{\"x\":\"\\u0000\"}", "{\"x\":\"\\ud800\"}", "{\"x\":\"\\z\"}",
        "{\"x\":\"\xc0\xaf\"}", "{\"x\":\"\xed\xa0\x80\"}", "{\"x\":-}", "[1,]", NULL};
    for (size_t i = 0; bad[i]; i++) assert(!mw_parse(bad[i], strlen(bad[i])));
    const char *good[] = {"{}", "[]", "{\"n\":-12.5e+1}", "{\"x\":\"\\\\u0000\"}",
        "{\"x\":\"café 😀\"}", "{\"x\":\"\\ud83d\\ude00\"}", "[true,false,null]", NULL};
    for (size_t i = 0; good[i]; i++) { cJSON *v = mw_parse(good[i], strlen(good[i])); assert(v); cJSON_Delete(v); }
    char deep[100]; memset(deep, '[', 40); memset(deep + 40, ']', 40); assert(!mw_parse(deep, 80));
    char nul[] = {'{', '}', 0, 'x'}; assert(!mw_parse(nul, sizeof(nul)));
    puts("strict JSON and UTF-8 checks passed");
}
static void text_cases(void) {
    const char *raw = "A\bA _\bB \033[31mred\033[0m\tword\n\033]8;;https://example.test\aLink\033]8;;\a\n café 😀";
    char *clean = mw_clean(raw, strlen(raw)); assert(clean);
    assert(!strchr(clean, '\b') && !strchr(clean, 27));
    assert(strstr(clean, "A B red") && strstr(clean, "Link") && strstr(clean, "café 😀")); free(clean);
    const char *page = "NAME\n  a test\nOPTIONS\n  --flag  use it\n  --other\nEND\n";
    cJSON *r = mw_excerpt(page, 3, 2);
    assert(!strcmp(mw_string(mw_get(r, "text")), "OPTIONS\n  --flag  use it\n"));
    assert(mw_get(r, "next_start_line")->valueint == 5); cJSON_Delete(r);
    r = mw_find(page, "--FLAG", 1, 5, 1);
    const cJSON *matches = mw_get(r, "matches"); assert(cJSON_GetArraySize(matches) == 1);
    assert(mw_get(matches->child, "line")->valueint == 4); cJSON_Delete(r);
    r = mw_find(page, "never", 1, 5, 1); assert(cJSON_GetArraySize(mw_get(r, "matches")) == 0); cJSON_Delete(r);
    assert(mw_topic("git-commit") && mw_topic("SSL_read") && mw_topic("std::thing") == 0);
    assert(!mw_topic("--help") && !mw_topic("../ls") && !mw_topic("a;id") && !mw_topic("$(id)"));
    assert(mw_section("3ssl") && mw_section("n") && !mw_section("-P") && !mw_section("1:2"));
    puts("formatting, lookup and pagination checks passed");
}
static void check_pages(const char *clean) {
    size_t start = 1, bytes = 0, pages = 0;
    do {
        cJSON *r = mw_excerpt(clean, start, 200);
        bytes += strlen(mw_string(mw_get(r, "text"))); pages++;
        const cJSON *next = mw_get(r, "next_start_line"); size_t previous = start;
        start = cJSON_IsNull(next) ? 0 : (size_t)next->valuedouble;
        assert(!start || start > previous); cJSON_Delete(r);
    } while (start);
    assert(bytes == strlen(clean) && pages > 1);
}
static void boundaries(void) {
    mw_buffer b = {.limit = 1024};
    char input[1025]; memset(input, 'a', sizeof(input));
    assert(mw_append(&b, input, 1024)); assert(!mw_append(&b, input, 1));
    assert(b.size == 1024 && b.data[1024] == 0); mw_buffer_free(&b);
    char *large = mw_alloc(MW_CAPTURE + 1); memset(large, 'x', MW_CAPTURE);
    char *clean = mw_clean(large, MW_CAPTURE); assert(clean); assert(mw_utf8(clean, strlen(clean)));
    check_pages(clean); free(clean);
    /* Expanded tabs and replacement characters must also permit pagination. */
    memset(large, '\t', 32768); clean = mw_clean(large, 32768); assert(clean);
    check_pages(clean); free(clean);
    memset(large, 255, 32768); clean = mw_clean(large, 32768); assert(clean);
    check_pages(clean);
    free(clean); free(large);
    puts("buffer and page boundary checks passed");
}
static uint32_t random_word(uint32_t *state) { *state = *state * 1664525U + 1013904223U; return *state; }
static void fuzz(void) {
    uint32_t state = 0x426f756eU;
    char data[4096];
    for (size_t i = 0; i < 10000; i++) {
        size_t n = random_word(&state) % sizeof(data);
        for (size_t j = 0; j < n; j++) data[j] = (char)(random_word(&state) >> 24);
        cJSON *json = mw_parse(data, n); cJSON_Delete(json);
        char *clean = mw_clean(data, n); assert(clean);
        assert(mw_utf8(clean, strlen(clean)) && !strchr(clean, '\b') && !strchr(clean, 27));
        free(clean);
    }
    puts("10000 deterministic byte-input cases passed");
}
int main(int argc, char **argv) {
    const char *mode = argc > 1 ? argv[1] : "all";
    if (!strcmp(mode, "parser") || !strcmp(mode, "all")) parser();
    if (!strcmp(mode, "text") || !strcmp(mode, "all")) text_cases();
    if (!strcmp(mode, "boundaries") || !strcmp(mode, "all")) boundaries();
    if (!strcmp(mode, "fuzz") || !strcmp(mode, "all")) fuzz();
    return 0;
}
