#include <errno.h>
#include <poll.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "backend.h"

int verbose;

static void vlog(const char *fmt, va_list ap)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm tm;
    localtime_r(&ts.tv_sec, &tm);
    fprintf(stderr, "%02d:%02d:%02d.%03ld backend: ", tm.tm_hour, tm.tm_min, tm.tm_sec,
            ts.tv_nsec / 1000000);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
}

void log_info(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vlog(fmt, ap);
    va_end(ap);
}

void log_debug(const char *fmt, ...)
{
    if (!verbose)
        return;
    va_list ap;
    va_start(ap, fmt);
    vlog(fmt, ap);
    va_end(ap);
}

static double now_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

/* ---- --bench: capture as fast as the compositor delivers ---------------- */

struct bench {
    struct wl_ctx *wl;
    double start, last_report;
    long frames, pixels, frame_pixels;
    bool stop;
};

static void bench_frame(void *data, struct frame *f)
{
    struct bench *b = data;
    long px = 0;
    if (f->full_damage || f->n_damage == 0)
        px = (long)f->width * f->height;
    for (int i = 0; i < f->n_damage && !f->full_damage; i++)
        px += (long)f->damage[i].w * f->damage[i].h;
    b->frames++;
    b->pixels += px;
    b->frame_pixels = (long)f->width * f->height;
    double t = now_s();
    if (t - b->last_report >= 1.0) {
        log_info("%.1f fps, %.1f%% of the screen changed per frame", b->frames / (t - b->start),
                 100.0 * b->pixels / b->frames / b->frame_pixels);
        b->last_report = t;
    }
    wl_request_frame(b->wl, false);
}

static void bench_cursor(void *data, struct cursor *c)
{
    log_info("cursor %dx%d hotspot %d,%d", c->width, c->height, c->hot_x, c->hot_y);
}

static void bench_resized(void *data, int w, int h)
{
    log_info("output %dx%d", w, h);
}

static void bench_stopped(void *data)
{
    ((struct bench *)data)->stop = true;
}

static int run_bench(const char *output, double seconds)
{
    static const struct wl_callbacks cb = {
        .frame_ready = bench_frame,
        .cursor_changed = bench_cursor,
        .output_resized = bench_resized,
        .stopped = bench_stopped,
    };
    struct bench b = {0};
    b.wl = wl_start(output, &cb, &b);
    if (!b.wl)
        return 1;
    b.start = b.last_report = now_s();
    wl_request_frame(b.wl, true);
    while (!b.stop && now_s() - b.start < seconds) {
        if (wl_prepare(b.wl) < 0)
            return 1;
        struct pollfd p = {.fd = wl_fd(b.wl), .events = POLLIN};
        int r = poll(&p, 1, 200);
        if (r < 0 && errno != EINTR)
            return 1;
        if (wl_after_poll(b.wl, r > 0 && (p.revents & POLLIN)) < 0)
            return 1;
    }
    log_info("%ld frames in %.1fs", b.frames, now_s() - b.start);
    return 0;
}

static void usage(void)
{
    fprintf(stderr,
            "Usage: omarchy-xrdp-backend [-v] --socket PATH [--output NAME] [--resize-cmd CMD]\n"
            "       omarchy-xrdp-backend [-v] --bench SECONDS [--output NAME]\n");
}

int xup_main(const char *socket_path, const char *output, const char *resize_cmd);

int main(int argc, char **argv)
{
    const char *socket_path = NULL, *output = "HEADLESS-RDP", *resize_cmd = NULL;
    double bench = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-v"))
            verbose = 1;
        else if (!strcmp(argv[i], "--socket") && i + 1 < argc)
            socket_path = argv[++i];
        else if (!strcmp(argv[i], "--resize-cmd") && i + 1 < argc)
            resize_cmd = argv[++i];
        else if (!strcmp(argv[i], "--output") && i + 1 < argc)
            output = argv[++i];
        else if (!strcmp(argv[i], "--bench") && i + 1 < argc)
            bench = atof(argv[++i]);
        else {
            usage();
            return 2;
        }
    }
    if (bench > 0)
        return run_bench(output, bench);
    if (!socket_path) {
        usage();
        return 2;
    }
    return xup_main(socket_path, output, resize_cmd);
}
