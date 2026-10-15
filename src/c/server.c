#include "manwise.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static volatile sig_atomic_t stopped;
static void stop_handler(int signum) { stopped = signum; }
static int flush(mw_server *s) {
    ssize_t n = write(1, s->output.data + s->sent, s->output.size - s->sent);
    if (n > 0) s->sent += (size_t)n;
    else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) return 0;
    if (s->sent == s->output.size) { s->sent = 0; s->output.size = 0; }
    return 1;
}
int mw_serve(mw_server *s) {
    struct sigaction action = {0}; action.sa_handler = stop_handler; sigemptyset(&action.sa_mask);
    if (sigaction(SIGTERM, &action, NULL) || sigaction(SIGINT, &action, NULL)) return 2;
    action.sa_handler = SIG_IGN; if (sigaction(SIGPIPE, &action, NULL)) return 2;
    action.sa_handler = SIG_DFL; if (sigaction(SIGCHLD, &action, NULL)) return 2;
    int input_flags = fcntl(0, F_GETFL), output_flags = fcntl(1, F_GETFL);
    if (input_flags < 0 || output_flags < 0) return 2;
    if (fcntl(0, F_SETFL, input_flags | O_NONBLOCK) || fcntl(1, F_SETFL, output_flags | O_NONBLOCK)) return 2;
    s->output.limit = MW_QUEUE;
    char *input = mw_alloc(MW_INPUT + 1);
    size_t used = 0; int dropping = 0, failed = 0;
    double last_write = mw_now();
    while (!s->closing && !stopped) {
        mw_manual_poll(s);
        int pending = s->output.size > s->sent;
        if (!pending) last_write = mw_now();
        else if (mw_now() - last_write > 5000) { failed = 1; break; }
        struct pollfd fds[] = {
            {0, POLLIN, 0}, {1, pending ? POLLOUT : 0, 0},
            {s->job.id ? s->job.process.out_fd : -1, POLLIN, 0},
            {s->job.id ? s->job.process.err_fd : -1, POLLIN, 0}
        };
        int ready = poll(fds, 4, s->job.id ? 20 : pending ? 1000 : -1);
        if (ready < 0) { if (errno == EINTR) continue; failed = 1; break; }
        if (fds[1].revents & (POLLHUP | POLLERR | POLLNVAL)) { failed = 1; break; }
        if (fds[1].revents & POLLOUT) {
            size_t before = s->output.size - s->sent;
            if (!flush(s)) { failed = 1; break; }
            if (s->output.size - s->sent < before) last_write = mw_now();
        }
        if (fds[0].revents & (POLLIN | POLLHUP)) {
            char chunk[8192]; ssize_t n = read(0, chunk, sizeof(chunk));
            if (!n) break;
            if (n < 0) { if (errno == EINTR || errno == EAGAIN) continue; failed = 1; break; }
            for (size_t i = 0; i < (size_t)n && !s->closing; i++) {
                if (chunk[i] == '\n') {
                    if (!dropping && used) mw_dispatch(s, input, used);
                    dropping = 0; used = 0;
                } else if (!dropping) {
                    if (used == MW_INPUT) {
                        dropping = 1; used = 0;
                        mw_error(s, NULL, -32700, "Message exceeds 65536 bytes", NULL);
                    } else input[used++] = chunk[i];
                }
            }
        }
        if (fds[0].revents & (POLLERR | POLLNVAL)) { failed = 1; break; }
    }
    mw_manual_cancel(s);
    double deadline = mw_now() + 250;
    while (!stopped && s->output.size > s->sent && mw_now() < deadline) {
        struct pollfd out = {1, POLLOUT, 0};
        if (poll(&out, 1, 10) > 0 && (out.revents & POLLOUT)) { if (!flush(s)) break; }
        if (out.revents & (POLLERR | POLLHUP | POLLNVAL)) break;
    }
    fcntl(0, F_SETFL, input_flags); fcntl(1, F_SETFL, output_flags);
    free(input);
    return failed || s->closing ? 2 : 0;
}
