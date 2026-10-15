#ifndef MANWISE_H
#define MANWISE_H

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif
#ifndef _DARWIN_C_SOURCE
#define _DARWIN_C_SOURCE
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include "cJSON.h"
#include <stddef.h>
#include <sys/types.h>

#define MW_VERSION "0.1.0"
#define MW_MODERN "2026-07-28"
#define MW_LEGACY "2025-11-25"
#ifndef MW_MAN
#define MW_MAN "/usr/bin/man"
#endif
#define MW_INPUT 65536U
#define MW_QUEUE 1048576U
#define MW_CAPTURE 2097152U
#define MW_EXCERPT 24000U
#define MW_CACHE_SLOTS 8U
#define MW_CACHE_TTL 60000.0

typedef struct { char *data; size_t size, capacity, limit; } mw_buffer;
typedef struct {
    pid_t pid;
    int out_fd, err_fd, status;
    mw_buffer out, err;
    double deadline;
    const char *error;
} mw_process;
typedef struct {
    char topic[129], section[17];
    char *text, *source;
    size_t size;
    double created, used;
} mw_page;
typedef struct {
    char tool[32], topic[129], section[17], query[257];
    size_t start, count, context;
    int refresh;
} mw_request;
typedef struct {
    mw_process process;
    cJSON *id;
    int modern, phase;
    mw_request request;
    char *source;
    double deadline;
} mw_job;
typedef struct {
    char *manpath;
    unsigned timeout_ms;
    int initialized, ready, closing;
    mw_job job;
    mw_page cache[MW_CACHE_SLOTS];
    mw_buffer output;
    size_t sent;
} mw_server;

void *mw_alloc(size_t size);
char *mw_copy(const char *text);
_Noreturn void mw_fatal(const char *message);
double mw_now(void);
int mw_append(mw_buffer *buffer, const char *data, size_t size);
void mw_buffer_free(mw_buffer *buffer);
cJSON *mw_parse(const char *data, size_t size);
int mw_utf8(const char *data, size_t size);
const cJSON *mw_get(const cJSON *object, const char *key);
const char *mw_string(const cJSON *value);
void mw_add(cJSON *object, const char *key, cJSON *value);
void mw_push(cJSON *array, cJSON *value);
void mw_str(cJSON *object, const char *key, const char *value);
void mw_num(cJSON *object, const char *key, double value);
int mw_keys(const cJSON *object, const char *const *keys);
int mw_integer(const cJSON *value, long long min, long long max, long long *out);
char *mw_clean(const char *data, size_t size);
int mw_topic(const char *topic);
int mw_section(const char *section);
cJSON *mw_excerpt(const char *text, size_t start, size_t count);
cJSON *mw_find(const char *text, const char *query, size_t start, size_t count, size_t context);

int mw_spawn(mw_process *process, char *const *argv, double deadline);
int mw_process_poll(mw_process *process);
void mw_process_stop(mw_process *process);
void mw_process_free(mw_process *process);

cJSON *mw_tools(void);
void mw_manual_call(mw_server *server, const cJSON *id, int modern, const cJSON *params);
void mw_manual_poll(mw_server *server);
void mw_manual_cancel(mw_server *server);
void mw_manual_free(mw_server *server);
void mw_result(mw_server *server, const cJSON *id, int modern, cJSON *result);
void mw_error(mw_server *server, const cJSON *id, int code, const char *message, cJSON *data);
void mw_tool_result(mw_server *server, const cJSON *id, int modern, cJSON *data, int error);
void mw_tool_error(mw_server *server, const cJSON *id, int modern, const char *code, const char *message);
void mw_dispatch(mw_server *server, const char *data, size_t size);
int mw_serve(mw_server *server);

#endif
