#include "manwise.h"
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static void wait_forever(void) { for (;;) pause(); }
int main(int argc, char **argv) {
    const char *root = NULL, *topic = argc > 1 ? argv[argc - 1] : "";
    int where = 0, search = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-M") && i + 1 < argc) root = argv[++i];
        else if (!strcmp(argv[i], "-w")) where = 1;
        else if (!strcmp(argv[i], "-k")) search = 1;
    }
    if (!strcmp(topic, "missing")) { fputs("No manual entry for missing\n", stderr); return 1; }
    if (!strcmp(topic, "broken")) { fputs("formatter failed\n", stderr); return 2; }
    if (search) {
        if (!strcmp(topic, "absent")) { fputs("absent: nothing appropriate\n", stderr); return 1; }
        printf("topic(1) - literal query %s\n", topic); return 0;
    }
    if (!strcmp(topic, "placeholder")) { puts("This system has been minimized; install manuals."); return 0; }
    if (where) { puts(argv[0]); return 0; }
    if (!strcmp(topic, "hang")) {
        pid_t child = fork();
        if (child < 0) return 2;
        if (!child) wait_forever();
        if (root) {
            char path[9000]; int n = snprintf(path, sizeof(path), "%s/pids", root);
            if (n < 0 || (size_t)n >= sizeof(path)) return 2;
            FILE *f = fopen(path, "w");
            if (!f) return 2;
            fprintf(f, "%ld %ld\n", (long)getpid(), (long)child); fclose(f);
        }
        wait_forever();
    }
    if (!strcmp(topic, "flood") || !strcmp(topic, "errflood")) {
        char chunk[8192]; memset(chunk, 'x', sizeof(chunk));
        FILE *f = !strcmp(topic, "flood") ? stdout : stderr;
        for (int i = 0; i < 400; i++) if (fwrite(chunk, 1, sizeof(chunk), f) != sizeof(chunk)) return 2;
        return 0;
    }
    if (!strcmp(topic, "environment")) {
        const char *keys[] = {"MANOPT", "MANPATH", "LD_PRELOAD", "NROFF", "SECRET", NULL};
        for (size_t i = 0; keys[i]; i++) if (getenv(keys[i])) return 2;
        printf("ENVIRONMENT\nPAGER=%s\nPATH=%s\n", getenv("PAGER"), getenv("PATH")); return 0;
    }
    if (!strcmp(topic, "descriptors")) {
        for (int fd = 3; fd < 128; fd++) if (fcntl(fd, F_GETFD) >= 0) return 2;
        puts("Only standard descriptors are open."); return 0;
    }
    if (!strcmp(topic, "long")) { for (size_t i = 0; i < 50000; i++) putchar('x'); return 0; }
    if (!strcmp(topic, "cached") && root) {
        char path[9000]; int n = snprintf(path, sizeof(path), "%s/count", root);
        if (n < 0 || (size_t)n >= sizeof(path)) return 2;
        int count = 0; FILE *f = fopen(path, "r");
        if (f) { if (fscanf(f, "%d", &count) != 1) count = 0; fclose(f); }
        f = fopen(path, "w"); if (!f) return 2;
        fprintf(f, "%d\n", count + 1); fclose(f);
        printf("render %d\n", count + 1); return 0;
    }
    puts("TEST(1)\n\nNAME\n  test - synthetic manual\nOPTIONS\n  --alpha  First option\n  --beta   Second option\n  --alpha  Repeated option\nEND");
    return 0;
}
