#include "manwise.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static mw_server server;
static void cleanup(void) {
    mw_manual_free(&server); mw_buffer_free(&server.output); free(server.manpath);
}
static const char help[] =
    "Manwise - System manuals for informed agents\n\n"
    "Usage: manwise [serve] [--manpath DIR] [--timeout-ms N]\n"
    "       manwise client-config [--manpath DIR] [--timeout-ms N]\n"
    "       manwise --help | --version\n\n"
    "serve          Run the local MCP server over stdio (default).\n"
    "client-config  Print connection JSON using this executable's absolute path.\n"
    "--manpath DIR  Search only these installed manual directories; repeat to add.\n"
    "--timeout-ms N Set the whole lookup deadline (100-10000; default 5000).\n\n"
    "Tools: search_manuals, read_manual, find_in_manual.\n"
    "Requires macOS or Linux with /usr/bin/man. No documented command is executed.\n";

static char *executable_path(const char *arg) {
    if (strchr(arg, '/')) return realpath(arg, NULL);
    const char *path = getenv("PATH");
    if (!path) return NULL;
    char *copy = mw_copy(path), *save = NULL, *result = NULL;
    for (char *dir = strtok_r(copy, ":", &save); dir; dir = strtok_r(NULL, ":", &save)) {
        size_t n = strlen(dir) + strlen(arg) + 2;
        char *candidate = mw_alloc(n);
        snprintf(candidate, n, "%s/%s", dir, arg);
        if (!access(candidate, X_OK)) result = realpath(candidate, NULL);
        free(candidate); if (result) break;
    }
    free(copy); return result;
}
int main(int argc, char **argv) {
    server.timeout_ms = 5000;
    if (atexit(cleanup)) return 2;
    int config = argc > 1 && !strcmp(argv[1], "client-config");
    int begin = argc > 1 && (config || !strcmp(argv[1], "serve")) ? 2 : 1;
    if (argc == 2 && !strcmp(argv[1], "--help")) { fputs(help, stdout); return 0; }
    if (argc == 2 && !strcmp(argv[1], "--version")) { puts("manwise " MW_VERSION); return 0; }
    mw_buffer path = {.limit = 8192};
    for (int i = begin; i < argc; i++) {
        if (!strcmp(argv[i], "--manpath") && i + 1 < argc) {
            const char *dir = argv[++i]; struct stat st;
            char *resolved = *dir == '/' ? realpath(dir, NULL) : NULL;
            if (!resolved || strchr(resolved, ':') || stat(resolved, &st) || !S_ISDIR(st.st_mode)) {
                free(resolved); mw_buffer_free(&path); mw_fatal("--manpath needs an absolute directory without ':'");
            }
            if ((path.size && !mw_append(&path, ":", 1)) || !mw_append(&path, resolved, strlen(resolved))) {
                free(resolved); mw_buffer_free(&path); mw_fatal("manual directory list is too long");
            }
            free(resolved);
        } else if (!strcmp(argv[i], "--timeout-ms") && i + 1 < argc) {
            char *end = NULL; const char *value = argv[++i]; errno = 0;
            unsigned long n = strtoul(value, &end, 10);
            if (errno || !*value || *end || n < 100 || n > 10000) { mw_buffer_free(&path); mw_fatal("timeout must be 100-10000 milliseconds"); }
            server.timeout_ms = (unsigned)n;
        } else { mw_buffer_free(&path); fputs(help, stderr); return 2; }
    }
    server.manpath = path.data;
    if (config) {
        char *binary = executable_path(argv[0]); if (!binary) mw_fatal("cannot resolve executable path");
        cJSON *root = cJSON_CreateObject(), *servers = cJSON_CreateObject(), *entry = cJSON_CreateObject(), *args = cJSON_CreateArray();
        mw_str(entry, "command", binary); free(binary);
        mw_push(args, cJSON_CreateString("serve"));
        for (int i = begin; i < argc; i++) mw_push(args, cJSON_CreateString(argv[i]));
        mw_add(entry, "args", args); mw_add(servers, "manwise", entry); mw_add(root, "mcpServers", servers);
        char *text = cJSON_Print(root); if (!text) mw_fatal("out of memory");
        puts(text); free(text); cJSON_Delete(root); return 0;
    }
    if (access(MW_MAN, X_OK)) mw_fatal("install the system man command before starting Manwise");
    return mw_serve(&server);
}
