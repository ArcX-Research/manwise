#include "manwise.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

static void close_fd(int *fd) { if (*fd >= 0) { close(*fd); *fd = -1; } }
static int protect_descriptors(void) {
    /* Enumerate actual descriptors in the single-threaded parent. CLOEXEC avoids
     * an OPEN_MAX-sized close loop (often a million syscalls per launch). */
    DIR *directory = opendir("/dev/fd");
    if (!directory) directory = opendir("/proc/self/fd");
    if (!directory) return 0;
    int ok = 1;
    struct dirent *entry;
    for (;;) {
        errno = 0;
        entry = readdir(directory);
        if (!entry) { if (errno) ok = 0; break; }
        char *end = NULL;
        long fd = strtol(entry->d_name, &end, 10);
        if (!*entry->d_name || *end || fd < 3 || fd > 2147483647) continue;
        int flags = fcntl((int)fd, F_GETFD);
        if (flags < 0 || fcntl((int)fd, F_SETFD, flags | FD_CLOEXEC)) { ok = 0; break; }
    }
    closedir(directory);
    return ok;
}
int mw_spawn(mw_process *p, char *const *argv, double deadline) {
    int out[2], err[2];
    memset(p, 0, sizeof(*p)); p->out_fd = -1; p->err_fd = -1;
    p->out.limit = MW_CAPTURE; p->err.limit = 16384;
    if (pipe(out)) return 0;
    if (pipe(err)) { close(out[0]); close(out[1]); return 0; }
    if (!protect_descriptors()) { close(out[0]); close(out[1]); close(err[0]); close(err[1]); return 0; }
    pid_t pid = fork();
    if (!pid) {
        struct sigaction action = {0};
        action.sa_handler = SIG_DFL; sigemptyset(&action.sa_mask);
        sigaction(SIGTERM, &action, NULL); sigaction(SIGINT, &action, NULL); sigaction(SIGPIPE, &action, NULL);
        sigset_t mask; sigemptyset(&mask); sigprocmask(SIG_SETMASK, &mask, NULL);
        if (setpgid(0, 0) || chdir("/")) _exit(126);
        int nullfd = open("/dev/null", O_RDONLY);
        if (nullfd < 0 || dup2(nullfd, 0) < 0 || dup2(out[1], 1) < 0 || dup2(err[1], 2) < 0) _exit(126);
        if (nullfd > 2) close(nullfd);
        struct rlimit core = {0, 0}, cpu = {5, 5};
        if (setrlimit(RLIMIT_CORE, &core) || setrlimit(RLIMIT_CPU, &cpu)) _exit(126);
        char *const env[] = {
            "PATH=/usr/bin:/bin:/usr/sbin:/sbin", "LANG=C", "LC_ALL=C", "TERM=dumb",
            "MANPAGER=/bin/cat", "PAGER=/bin/cat", "MANWIDTH=100", "COLUMNS=100",
            "GROFF_NO_SGR=1", "MAN_KEEP_FORMATTING=0", NULL
        };
        execve(MW_MAN, argv, env);
        _exit(127);
    }
    close(out[1]); close(err[1]);
    if (pid < 0) { close(out[0]); close(err[0]); return 0; }
    p->pid = pid; p->out_fd = out[0]; p->err_fd = err[0]; p->deadline = deadline;
    if (setpgid(pid, pid) && errno != EACCES && errno != ESRCH) { mw_process_stop(p); return 0; }
    if (fcntl(out[0], F_SETFL, O_NONBLOCK) || fcntl(err[0], F_SETFL, O_NONBLOCK)) {
        mw_process_stop(p); return 0;
    }
    return 1;
}
void mw_process_stop(mw_process *p) {
    if (p->pid > 0) {
        kill(-p->pid, SIGKILL);
        /* The direct child has not been reaped, so its PID cannot be reused. */
        kill(p->pid, SIGKILL);
        while (waitpid(p->pid, &p->status, 0) < 0 && errno == EINTR) {}
        p->pid = 0;
    }
    close_fd(&p->out_fd); close_fd(&p->err_fd);
}
static int drain(int *fd, mw_buffer *b) {
    if (*fd < 0) return 1;
    char chunk[8192];
    for (unsigned i = 0; i < 8; i++) {
        ssize_t n = read(*fd, chunk, sizeof(chunk));
        if (n > 0) { if (!mw_append(b, chunk, (size_t)n)) return 0; }
        else if (!n) { close_fd(fd); break; }
        else if (errno == EAGAIN || errno == EWOULDBLOCK) break;
        else if (errno != EINTR) return 0;
    }
    return 1;
}
int mw_process_poll(mw_process *p) {
    if (!p->pid) return 1;
    if (!drain(&p->out_fd, &p->out) || !drain(&p->err_fd, &p->err)) p->error = "output_limit";
    if (mw_now() >= p->deadline) p->error = "timeout";
    if (p->error) { mw_process_stop(p); return 1; }
    /* Keep the leader unreaped until both pipes close and its group is stopped. */
    if (p->out_fd < 0 && p->err_fd < 0) {
        siginfo_t info; memset(&info, 0, sizeof(info));
        if (waitid(P_PID, (id_t)p->pid, &info, WEXITED | WNOHANG | WNOWAIT) == 0 && info.si_pid) {
            kill(-p->pid, SIGKILL);
            while (waitpid(p->pid, &p->status, 0) < 0 && errno == EINTR) {}
            p->pid = 0; return 1;
        }
    }
    return 0;
}
void mw_process_free(mw_process *p) {
    mw_process_stop(p); mw_buffer_free(&p->out); mw_buffer_free(&p->err);
}
