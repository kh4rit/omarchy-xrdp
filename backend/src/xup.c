/* The xorgxrdp side of xrdp's "xup" module protocol: xrdp connects to our
 * Unix socket, sends input and client info, and we send frames as shared
 * memory for xrdp's encoder (H.264 or RemoteFX via RDPGFX, or plain
 * bitmaps), one frame in flight at a time. */
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/input-event-codes.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#include <xrdp_client_info.h>
#include <xrdp_constants.h>

#include "backend.h"
#include "clipboard.h"
#include "color.h"

/* xrdp → server message types */
#define MSG_EVENT 103
#define MSG_CLIENT_INFO 104
#define MSG_FRAME_ACK 106
#define MSG_SUPPRESS 108

/* 103 sub-messages */
#define EV_KEYDOWN 15
#define EV_KEYUP 16
#define EV_KEYSYNC 17
#define EV_MOUSEMOVE 100
#define EV_LBUTTONUP 101
#define EV_LBUTTONDOWN 102
#define EV_RBUTTONUP 103
#define EV_RBUTTONDOWN 104
#define EV_MBUTTONUP 105
#define EV_MBUTTONDOWN 106
#define EV_WHEELUP_DOWN 108
#define EV_WHEELDOWN_DOWN 110
#define EV_HWHEELLEFT_DOWN 112
#define EV_HWHEELRIGHT_DOWN 114
#define EV_BUTTON8UP 115
#define EV_BUTTON8DOWN 116
#define EV_BUTTON9UP 117
#define EV_BUTTON9DOWN 118
#define EV_VSCROLL 140
#define EV_HSCROLL 141
#define EV_INVALIDATE 200
#define EV_VERSION 301
#define EV_MONITOR_RESIZE 302

/* RDP keyboard flags */
#define KBD_FLAG_EXT 0x0100
#define KBD_FLAG_EXT1 0x0200
#define KBD_FLAG_UP 0x8000

/* GFX commands */
#define GFX_WIRETOSURFACE_1 0x0001
#define GFX_WIRETOSURFACE_2 0x0002
#define GFX_STARTFRAME 0x000b
#define GFX_ENDFRAME 0x000c
#define GFX_CODEC_AVC420 0x000b
#define GFX_CODEC_PROGRESSIVE 0x0009
#define GFX_FORMAT_XRGB 0x20

#define MAX_MESSAGE 65536
#define CURSOR_SMALL 32
#define CURSOR_LARGE 96

struct server {
    struct wl_ctx *wl;
    const char *socket_path;
    const char *resize_cmd;
    int listen_fd;
    int conn_fd;

    uint8_t *in;
    size_t in_len, in_cap;

    bool have_info;
    struct xrdp_client_info ci;
    int width, height; /* session size */

    /* shared memory handed to xrdp for converted frames */
    int shm_fd;
    uint8_t *shm;
    size_t shm_size;
    bool shm_dirty_all; /* needs a full conversion before use */

    int32_t frame_id, frame_ack;
    bool capture_pending;
    bool want_full;
    bool suppressed;
    double last_send;
    double interval;

    struct frame *last_frame; /* most recent capture, still in the capture buffer */
    struct cursor *cursor;
    bool cursor_dirty;

    uint8_t key_down[KEY_MAX + 1];
    bool pause_ext1;
    bool stop;

    long frames_sent;
    double stats_start;
    double sent_at;       /* when the frame in flight was sent */
    double ack_wait_sum;  /* seconds from send to ack, summed over frames */
    double convert_sum;   /* seconds spent converting, summed */
    long acks;
};

static double now_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

/* ---- output: message building --------------------------------------------- */

struct msg {
    uint8_t buf[MAX_MESSAGE];
    size_t len;
    uint16_t count;
    size_t order_start;
};

static void put16(struct msg *m, uint16_t v)
{
    memcpy(m->buf + m->len, &v, 2);
    m->len += 2;
}

static void put32(struct msg *m, uint32_t v)
{
    memcpy(m->buf + m->len, &v, 4);
    m->len += 4;
}

static void put8(struct msg *m, uint8_t v)
{
    m->buf[m->len++] = v;
}

static void putrect(struct msg *m, struct rect r)
{
    put16(m, r.x);
    put16(m, r.y);
    put16(m, r.w);
    put16(m, r.h);
}

/* Type-3 order list: header now, sizes patched as orders end */
static void msg_begin(struct msg *m, uint16_t type)
{
    m->len = 8;
    m->count = 0;
    memset(m->buf, 0, 8);
    memcpy(m->buf, &type, 2);
}

static void order_begin(struct msg *m, uint16_t type)
{
    m->order_start = m->len;
    put16(m, type);
    put16(m, 0);
}

static void order_end(struct msg *m)
{
    uint16_t size = m->len - m->order_start;
    memcpy(m->buf + m->order_start + 2, &size, 2);
    m->count++;
}

static void msg_finish(struct msg *m)
{
    uint32_t len = m->len - 8;
    memcpy(m->buf + 2, &m->count, 2);
    memcpy(m->buf + 4, &len, 4);
}

static bool write_all(struct server *s, const void *data, size_t len)
{
    const uint8_t *p = data;
    while (len > 0) {
        ssize_t n = send(s->conn_fd, p, len, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            if (errno == EAGAIN) {
                struct pollfd pfd = {.fd = s->conn_fd, .events = POLLOUT};
                if (poll(&pfd, 1, 5000) <= 0)
                    return false;
                continue;
            }
            return false;
        }
        p += n;
        len -= n;
    }
    return true;
}

static void drop_connection(struct server *s, const char *why);

static bool msg_send(struct server *s, struct msg *m)
{
    msg_finish(m);
    if (!write_all(s, m->buf, m->len)) {
        drop_connection(s, "write failed");
        return false;
    }
    return true;
}

/* xrdp reads exactly 4 bytes plus one SCM_RIGHTS fd after an fd-bearing order */
static bool send_fd(struct server *s, int fd)
{
    char data[4] = "int";
    struct iovec iov = {.iov_base = data, .iov_len = 4};
    union {
        struct cmsghdr h;
        char buf[CMSG_SPACE(sizeof(int))];
    } ctrl;
    memset(&ctrl, 0, sizeof(ctrl));
    struct msghdr mh = {
        .msg_iov = &iov,
        .msg_iovlen = 1,
        .msg_control = ctrl.buf,
        .msg_controllen = sizeof(ctrl.buf),
    };
    struct cmsghdr *c = CMSG_FIRSTHDR(&mh);
    c->cmsg_level = SOL_SOCKET;
    c->cmsg_type = SCM_RIGHTS;
    c->cmsg_len = CMSG_LEN(sizeof(int));
    memcpy(CMSG_DATA(c), &fd, sizeof(int));
    for (;;) {
        ssize_t n = sendmsg(s->conn_fd, &mh, MSG_NOSIGNAL);
        if (n == 4)
            return true;
        if (n < 0 && (errno == EINTR || errno == EAGAIN)) {
            struct pollfd pfd = {.fd = s->conn_fd, .events = POLLOUT};
            poll(&pfd, 1, 1000);
            continue;
        }
        drop_connection(s, "sending the frame fd failed");
        return false;
    }
}

static void send_caps(struct server *s)
{
    static const uint8_t caps[8] = {2, 0, 0, 0, 0, 0, 0, 0};
    if (!write_all(s, caps, sizeof(caps)))
        drop_connection(s, "write failed");
}

/* Metadata 100 / cmd 3: answers client info and every monitor resize */
static void send_memory_allocation_complete(struct server *s)
{
    struct msg *m = malloc(sizeof(*m));
    msg_begin(m, 100);
    put16(m, 3);
    put16(m, 8);
    put16(m, s->width);
    put16(m, s->height);
    m->count = 1;
    msg_send(s, m);
    free(m);
}

/* ---- frames ---------------------------------------------------------------- */

static bool alloc_shm(struct server *s)
{
    size_t size;
    int code = s->ci.capture_code;
    if (code == CC_GFX_A2)
        size = (size_t)s->width * s->height * 2;
    else if (code == CC_GFX_PRO || code == CC_SUF_RFX)
        size = (size_t)((s->width + 63) & ~63) * ((s->height + 63) & ~63) * 4;
    else
        size = (size_t)s->width * s->height * 4;

    if (s->shm && s->shm_size == size) {
        s->shm_dirty_all = true;
        return true;
    }
    if (s->shm)
        munmap(s->shm, s->shm_size);
    if (s->shm_fd >= 0)
        close(s->shm_fd);
    s->shm = NULL;
    s->shm_fd = memfd_create("omarchy-xrdp-encode", MFD_CLOEXEC);
    if (s->shm_fd < 0 || ftruncate(s->shm_fd, size) < 0)
        return false;
    s->shm = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, s->shm_fd, 0);
    if (s->shm == MAP_FAILED) {
        s->shm = NULL;
        return false;
    }
    s->shm_size = size;
    s->shm_dirty_all = true;
    return true;
}

/* Clip to the session, expand to even coordinates (NV12 subsampling) */
static bool clip_rect(struct server *s, struct rect *r, bool even)
{
    int x1 = r->x, y1 = r->y, x2 = r->x + r->w, y2 = r->y + r->h;
    if (x1 < 0)
        x1 = 0;
    if (y1 < 0)
        y1 = 0;
    if (x2 > s->width)
        x2 = s->width;
    if (y2 > s->height)
        y2 = s->height;
    if (even) {
        x1 &= ~1;
        y1 &= ~1;
        x2 = (x2 + 1) & ~1;
        y2 = (y2 + 1) & ~1;
        if (x2 > (s->width & ~1))
            x2 = s->width & ~1;
        if (y2 > (s->height & ~1))
            y2 = s->height & ~1;
    }
    if (x2 <= x1 || y2 <= y1)
        return false;
    *r = (struct rect){x1, y1, x2 - x1, y2 - y1};
    return true;
}

/* Damage of this frame as session rects; full when asked or unknown */
static int frame_rects(struct server *s, struct frame *f, bool full, bool even, struct rect *out,
                       int max)
{
    int n = 0;
    if (full || f->full_damage || f->n_damage == 0) {
        out[0] = (struct rect){0, 0, s->width, s->height};
        return clip_rect(s, &out[0], even) ? 1 : 0;
    }
    for (int i = 0; i < f->n_damage && n < max; i++) {
        struct rect r = f->damage[i];
        if (clip_rect(s, &r, even))
            out[n++] = r;
    }
    return n;
}

static void send_gfx_h264(struct server *s, struct frame *f, bool full)
{
    struct rect rects[MAX_DAMAGE_RECTS];
    full = full || s->shm_dirty_all;
    int n = frame_rects(s, f, full, true, rects, MAX_DAMAGE_RECTS);
    if (n == 0)
        return;
    for (int i = 0; i < n; i++)
        nv12_convert_rect(s->shm, s->width, s->height, f->data, f->stride, f->width, f->height,
                          rects[i]);
    s->shm_dirty_all = false;

    int32_t id = ++s->frame_id;
    uint32_t wts_bytes = 8 + 9 + 2 + 8 * n + 2 + 8 * n + 8;
    uint32_t cmd_bytes = 16 + wts_bytes + 12;

    struct msg *m = malloc(sizeof(*m));
    msg_begin(m, 3);
    order_begin(m, 1);
    order_end(m);
    order_begin(m, 62);
    put32(m, cmd_bytes);
    /* STARTFRAME */
    put16(m, GFX_STARTFRAME);
    put16(m, 0);
    put32(m, 16);
    put32(m, id);
    put32(m, (uint32_t)(now_s() * 1000));
    /* WIRETOSURFACE_1: surface 0 = the single monitor */
    put16(m, GFX_WIRETOSURFACE_1);
    put16(m, 0);
    put32(m, wts_bytes);
    put16(m, 0);
    put16(m, GFX_CODEC_AVC420);
    put8(m, GFX_FORMAT_XRGB);
    put32(m, 0);
    put16(m, n);
    for (int i = 0; i < n; i++)
        putrect(m, rects[i]);
    put16(m, n);
    for (int i = 0; i < n; i++)
        putrect(m, rects[i]);
    put16(m, 0);
    put16(m, 0);
    put16(m, s->width);
    put16(m, s->height);
    /* ENDFRAME: must be last, or xrdp never acks */
    put16(m, GFX_ENDFRAME);
    put16(m, 0);
    put32(m, 12);
    put32(m, id);
    put32(m, s->shm_size);
    order_end(m);
    order_begin(m, 2);
    order_end(m);
    if (msg_send(s, m))
        send_fd(s, s->shm_fd);
    free(m);
}

/* RemoteFX: whole 64x64 tiles touched by the damage */
static int damaged_tiles(struct server *s, struct rect *rects, int n, struct rect *tiles, int max)
{
    int tw = (s->width + 63) / 64, th = (s->height + 63) / 64;
    static uint8_t *mark;
    static int mark_size;
    if (mark_size < tw * th) {
        free(mark);
        mark = calloc(tw * th, 1);
        mark_size = tw * th;
    } else {
        memset(mark, 0, tw * th);
    }
    for (int i = 0; i < n; i++) {
        struct rect r = rects[i];
        for (int ty = r.y / 64; ty <= (r.y + r.h - 1) / 64 && ty < th; ty++)
            for (int tx = r.x / 64; tx <= (r.x + r.w - 1) / 64 && tx < tw; tx++)
                mark[ty * tw + tx] = 1;
    }
    int count = 0;
    for (int i = 0; i < tw * th && count < max; i++)
        if (mark[i])
            tiles[count++] = (struct rect){(i % tw) * 64, (i / tw) * 64, 64, 64};
    return count;
}

#define MAX_TILES 3800

static void send_rfx(struct server *s, struct frame *f, bool full)
{
    struct rect rects[MAX_DAMAGE_RECTS];
    static struct rect tiles[MAX_TILES];
    full = full || s->shm_dirty_all;
    int n = frame_rects(s, f, full, false, rects, MAX_DAMAGE_RECTS);
    if (n == 0)
        return;
    int nt = damaged_tiles(s, rects, n, tiles, MAX_TILES);
    int tiles_per_row = (s->width + 63) / 64;
    for (int i = 0; i < nt; i++)
        rfx_convert_tile(s->shm, tiles_per_row, f->data, f->stride, f->width, f->height,
                         tiles[i].x / 64, tiles[i].y / 64);
    s->shm_dirty_all = false;

    int32_t id = ++s->frame_id;
    struct msg *m = malloc(sizeof(*m));
    msg_begin(m, 3);
    order_begin(m, 1);
    order_end(m);
    if (s->ci.capture_code == CC_GFX_PRO) {
        uint32_t wts_bytes = 8 + 13 + 2 + 8 * n + 2 + 8 * nt + 8;
        order_begin(m, 62);
        put32(m, 16 + wts_bytes + 12);
        put16(m, GFX_STARTFRAME);
        put16(m, 0);
        put32(m, 16);
        put32(m, id);
        put32(m, (uint32_t)(now_s() * 1000));
        put16(m, GFX_WIRETOSURFACE_2);
        put16(m, 0);
        put32(m, wts_bytes);
        put16(m, 0);
        put16(m, GFX_CODEC_PROGRESSIVE);
        put32(m, 0);
        put8(m, GFX_FORMAT_XRGB);
        put32(m, 0);
        put16(m, n);
        for (int i = 0; i < n; i++)
            putrect(m, rects[i]);
        put16(m, nt);
        for (int i = 0; i < nt; i++)
            putrect(m, tiles[i]);
        put16(m, 0);
        put16(m, 0);
        put16(m, s->width);
        put16(m, s->height);
        put16(m, GFX_ENDFRAME);
        put16(m, 0);
        put32(m, 12);
        put32(m, id);
        put32(m, s->shm_size);
    } else { /* CC_SUF_RFX: surface bits */
        order_begin(m, 64);
        put16(m, n);
        for (int i = 0; i < n; i++)
            putrect(m, rects[i]);
        put16(m, nt);
        for (int i = 0; i < nt; i++)
            putrect(m, tiles[i]);
        put32(m, 0);
        put32(m, id);
        put32(m, s->shm_size);
        put32(m, 0);
        put16(m, 0);
        put16(m, 0);
        put16(m, s->width);
        put16(m, s->height);
    }
    order_end(m);
    order_begin(m, 2);
    order_end(m);
    if (msg_send(s, m))
        send_fd(s, s->shm_fd);
    free(m);
}

/* Plain bitmaps (and JPEG): the capture buffer itself when its layout fits */
static void send_simple(struct server *s, struct frame *f, bool full)
{
    struct rect rects[16];
    int n = frame_rects(s, f, full, false, rects, MAX_DAMAGE_RECTS < 16 ? MAX_DAMAGE_RECTS : 16);
    if (f->n_damage > 15 && !full) {
        /* xorgxrdp sends the bounding box beyond 15 rects */
        struct rect all = {0, 0, s->width, s->height};
        clip_rect(s, &all, false);
        rects[0] = all;
        n = 1;
    }
    if (n == 0)
        return;

    int fd = f->fd;
    uint32_t bytes = f->size;
    int w = f->width, h = f->height;
    if (s->ci.capture_format == XRDP_a8b8g8r8 || f->width != s->width) {
        /* Needs reordering or a session-sized copy */
        for (int i = 0; i < n; i++) {
            struct rect r = rects[i];
            if (r.x + r.w > f->width)
                r.w = f->width - r.x;
            if (r.y + r.h > f->height)
                r.h = f->height - r.y;
            if (r.w <= 0 || r.h <= 0)
                continue;
            if (s->ci.capture_format == XRDP_a8b8g8r8)
                swizzle_rect(s->shm, s->width * 4, f->data, f->stride, r);
            else
                for (int y = r.y; y < r.y + r.h; y++)
                    memcpy(s->shm + (size_t)y * s->width * 4 + r.x * 4,
                           f->data + (size_t)y * f->stride + r.x * 4, r.w * 4);
        }
        fd = s->shm_fd;
        bytes = s->shm_size;
        w = s->width;
        h = s->height;
    }

    int32_t id = ++s->frame_id;
    struct msg *m = malloc(sizeof(*m));
    msg_begin(m, 3);
    order_begin(m, 1);
    order_end(m);
    order_begin(m, 64);
    put16(m, n);
    for (int i = 0; i < n; i++)
        putrect(m, rects[i]);
    put16(m, n);
    for (int i = 0; i < n; i++)
        putrect(m, rects[i]);
    put32(m, 0);
    put32(m, id);
    put32(m, bytes);
    put32(m, 0);
    put16(m, 0);
    put16(m, 0);
    put16(m, w);
    put16(m, h);
    order_end(m);
    order_begin(m, 2);
    order_end(m);
    if (msg_send(s, m))
        send_fd(s, fd);
    free(m);
}

static void send_frame(struct server *s, struct frame *f, bool full)
{
    double t0 = now_s();
    switch (s->ci.capture_code) {
    case CC_GFX_A2:
        send_gfx_h264(s, f, full);
        break;
    case CC_GFX_PRO:
    case CC_SUF_RFX:
        send_rfx(s, f, full);
        break;
    default:
        send_simple(s, f, full);
        break;
    }
    s->last_send = s->sent_at = now_s();
    s->convert_sum += s->last_send - t0;
    s->frames_sent++;
    if (verbose && s->last_send - s->stats_start >= 5) {
        log_debug("%.1f frames/s sent, %.1f ms to convert and send, %.1f ms until xrdp acks",
                  s->frames_sent / (s->last_send - s->stats_start),
                  1000 * s->convert_sum / s->frames_sent,
                  s->acks ? 1000 * s->ack_wait_sum / s->acks : 0);
        s->frames_sent = s->acks = 0;
        s->convert_sum = s->ack_wait_sum = 0;
        s->stats_start = s->last_send;
    }
}

static bool ready_to_send(struct server *s)
{
    return s->conn_fd >= 0 && s->have_info && s->shm && !s->suppressed &&
           s->frame_ack >= s->frame_id;
}

/* ---- cursor ---------------------------------------------------------------- */

static void send_cursor(struct server *s)
{
    struct cursor *c = s->cursor;
    if (!c || !c->pixels || s->conn_fd < 0 || !s->have_info || s->suppressed)
        return;
    s->cursor_dirty = false;

    bool large = c->width > CURSOR_SMALL || c->height > CURSOR_SMALL;
    bool large_ok = s->ci.large_pointer_support_flags & 1;
    int size = large && large_ok ? CURSOR_LARGE : CURSOR_SMALL;
    bool argb = s->ci.pointer_flags & 1;
    int Bpp = argb ? 4 : 3;
    size_t data_bytes = (size_t)size * size * Bpp, mask_bytes = (size_t)size * size / 8;
    uint8_t *data = calloc(1, data_bytes + mask_bytes);
    uint8_t *mask = data + data_bytes;
    if (!argb)
        memset(mask, 0xff, mask_bytes);

    /* Bottom-up rows; crop cursors larger than the slot */
    for (int y = 0; y < size && y < c->height; y++) {
        for (int x = 0; x < size && x < c->width; x++) {
            uint32_t p = c->pixels[(size_t)y * c->width + x];
            int row = size - 1 - y;
            if (argb) {
                memcpy(data + ((size_t)row * size + x) * 4, &p, 4);
            } else {
                uint8_t *d = data + ((size_t)row * size + x) * 3;
                d[0] = p & 0xff;
                d[1] = (p >> 8) & 0xff;
                d[2] = (p >> 16) & 0xff;
                if ((p >> 24) >= 0x80)
                    mask[(size_t)row * (size / 8) + x / 8] &= ~(0x80 >> (x % 8));
            }
        }
    }
    int hx = c->hot_x < size ? c->hot_x : size - 1, hy = c->hot_y < size ? c->hot_y : size - 1;

    struct msg *m = malloc(sizeof(*m));
    msg_begin(m, 3);
    order_begin(m, 1);
    order_end(m);
    if (size == CURSOR_SMALL) {
        order_begin(m, 51);
        put16(m, hx);
        put16(m, hy);
        put16(m, argb ? 32 : 0);
        memcpy(m->buf + m->len, data, data_bytes + mask_bytes);
        m->len += data_bytes + mask_bytes;
        order_end(m);
        order_begin(m, 2);
        order_end(m);
        msg_send(s, m);
    } else {
        int fd = memfd_create("omarchy-xrdp-cursor", MFD_CLOEXEC);
        if (fd >= 0 && write(fd, data, data_bytes + mask_bytes) == (ssize_t)(data_bytes + mask_bytes)) {
            order_begin(m, 63);
            put16(m, hx);
            put16(m, hy);
            put16(m, argb ? 32 : 0);
            put16(m, size);
            put16(m, size);
            order_end(m);
            order_begin(m, 2);
            order_end(m);
            if (msg_send(s, m))
                send_fd(s, fd);
        }
        if (fd >= 0)
            close(fd);
    }
    free(m);
    free(data);
}

/* ---- Wayland callbacks ------------------------------------------------------- */

static void on_frame(void *data, struct frame *f)
{
    struct server *s = data;
    s->capture_pending = false;
    s->last_frame = f;
    if (!ready_to_send(s)) {
        /* Not deliverable now: repaint everything once we can */
        s->want_full = true;
        return;
    }
    bool full = s->want_full;
    s->want_full = false;
    send_frame(s, f, full);
}

static void on_cursor(void *data, struct cursor *c)
{
    struct server *s = data;
    s->cursor = c;
    s->cursor_dirty = true;
    if (s->conn_fd >= 0 && s->have_info && !s->suppressed)
        send_cursor(s);
}

static void on_resized(void *data, int w, int h)
{
    struct server *s = data;
    s->want_full = true;
    log_info("output is %dx%d, session %dx%d", w, h, s->width, s->height);
}

static void on_stopped(void *data)
{
    struct server *s = data;
    log_info("output capture stopped");
    s->stop = true;
}

/* ---- resize ------------------------------------------------------------------ */

static void run_resize(struct server *s, int w, int h)
{
    int cw, ch;
    wl_get_size(s->wl, &cw, &ch);
    if (!s->resize_cmd || (cw == w && ch == h))
        return;
    char ws[16], hs[16];
    snprintf(ws, sizeof(ws), "%d", w);
    snprintf(hs, sizeof(hs), "%d", h);
    pid_t pid = fork();
    if (pid == 0) {
        execlp(s->resize_cmd, s->resize_cmd, ws, hs, (char *)NULL);
        _exit(127);
    }
    log_info("resizing output to %dx%d", w, h);
}

static void set_session_size(struct server *s, int w, int h)
{
    s->width = w;
    s->height = h;
    if (!alloc_shm(s))
        log_info("cannot allocate the %dx%d frame buffer", w, h);
    run_resize(s, w, h);
    s->want_full = true;
}

/* ---- input ----------------------------------------------------------------------- */

static void key_event(struct server *s, int scancode, uint32_t flags, bool down)
{
    bool ext = flags & KBD_FLAG_EXT;
    uint32_t code;

    if (flags & KBD_FLAG_EXT1) {
        /* E1 1D: first half of Pause; the 45 that follows is the key */
        if (scancode == 0x1d) {
            s->pause_ext1 = down;
            return;
        }
    }
    if (scancode == 0x45 && !ext &&
        (s->pause_ext1 || s->key_down[KEY_LEFTCTRL] || s->key_down[KEY_RIGHTCTRL]))
        code = KEY_PAUSE;
    else if (!ext && (scancode == 0x5b || scancode == 0x5c || scancode == 0x5d))
        code = rdp_scancode_to_evdev(scancode, true); /* Win/Menu sent without E0 */
    else if (!ext && scancode == 0x54)
        code = KEY_SYSRQ;
    else
        code = rdp_scancode_to_evdev(scancode, ext);
    if (!code || code > KEY_MAX) {
        log_debug("unmapped scancode 0x%x%s", scancode, ext ? " (E0)" : "");
        return;
    }

    if (down && s->key_down[code]) {
        /* Client auto-repeat: release and press again, like xorgxrdp */
        wl_key(s->wl, code, false);
    }
    if (!down && !s->key_down[code])
        return;
    s->key_down[code] = down;
    wl_key(s->wl, code, down);
}

static void release_all_keys(struct server *s)
{
    for (int code = 0; code <= KEY_MAX; code++) {
        if (s->key_down[code]) {
            s->key_down[code] = 0;
            wl_key(s->wl, code, false);
        }
    }
}

static void key_sync(struct server *s, uint32_t lock_flags)
{
    bool caps = lock_flags & 4, num = lock_flags & 2;
    bool caps_now, num_now;
    wl_get_locks(s->wl, &caps_now, &num_now);
    if (caps != caps_now) {
        wl_key(s->wl, KEY_CAPSLOCK, true);
        wl_key(s->wl, KEY_CAPSLOCK, false);
    }
    if (num != num_now) {
        wl_key(s->wl, KEY_NUMLOCK, true);
        wl_key(s->wl, KEY_NUMLOCK, false);
    }
}

static void handle_event(struct server *s, const uint8_t *p, size_t len)
{
    if (len < 20)
        return;
    uint32_t msg, p1, p2, p3, p4;
    memcpy(&msg, p, 4);
    memcpy(&p1, p + 4, 4);
    memcpy(&p2, p + 8, 4);
    memcpy(&p3, p + 12, 4);
    memcpy(&p4, p + 16, 4);

    switch (msg) {
    case EV_KEYDOWN:
    case EV_KEYUP:
        key_event(s, p3 & 0x7f, p4, msg == EV_KEYDOWN && !(p4 & KBD_FLAG_UP));
        return;
    case EV_KEYSYNC:
        key_sync(s, p1);
        return;
    case EV_MOUSEMOVE:
        wl_pointer_motion(s->wl, (int32_t)p1, (int32_t)p2);
        return;
    case EV_LBUTTONUP:
    case EV_LBUTTONDOWN:
        wl_pointer_motion(s->wl, (int32_t)p1, (int32_t)p2);
        wl_pointer_button(s->wl, BTN_LEFT, msg == EV_LBUTTONDOWN);
        return;
    case EV_RBUTTONUP:
    case EV_RBUTTONDOWN:
        wl_pointer_motion(s->wl, (int32_t)p1, (int32_t)p2);
        wl_pointer_button(s->wl, BTN_RIGHT, msg == EV_RBUTTONDOWN);
        return;
    case EV_MBUTTONUP:
    case EV_MBUTTONDOWN:
        wl_pointer_motion(s->wl, (int32_t)p1, (int32_t)p2);
        wl_pointer_button(s->wl, BTN_MIDDLE, msg == EV_MBUTTONDOWN);
        return;
    case EV_BUTTON8UP:
    case EV_BUTTON8DOWN:
        wl_pointer_button(s->wl, BTN_SIDE, msg == EV_BUTTON8DOWN);
        return;
    case EV_BUTTON9UP:
    case EV_BUTTON9DOWN:
        wl_pointer_button(s->wl, BTN_EXTRA, msg == EV_BUTTON9DOWN);
        return;
    /* Wheel as buttons (only when the client sends no delta): act on down */
    case EV_WHEELUP_DOWN:
        wl_pointer_axis(s->wl, false, -15, -1);
        return;
    case EV_WHEELDOWN_DOWN:
        wl_pointer_axis(s->wl, false, 15, 1);
        return;
    case EV_HWHEELLEFT_DOWN:
        wl_pointer_axis(s->wl, true, -15, -1);
        return;
    case EV_HWHEELRIGHT_DOWN:
        wl_pointer_axis(s->wl, true, 15, 1);
        return;
    case EV_VSCROLL:
    case EV_HSCROLL: {
        /* Signed RDP wheel delta, 120 per notch, positive = up / left */
        int32_t delta = (int32_t)p3;
        double notches = -delta / 120.0;
        int32_t discrete = delta % 120 == 0 ? -delta / 120 : 0;
        wl_pointer_axis(s->wl, msg == EV_HSCROLL, notches * 15, discrete);
        return;
    }
    case EV_INVALIDATE:
        s->want_full = true;
        return;
    case EV_VERSION:
        send_caps(s);
        return;
    case EV_MONITOR_RESIZE: {
        int w = (int)p1, h = (int)p2;
        log_info("client resized to %dx%d (%u monitors)", w, h, p3);
        if (w > 0 && h > 0 && w <= 8192 && h <= 8192)
            set_session_size(s, w, h);
        /* Always answer, or xrdp's resize hangs forever */
        send_memory_allocation_complete(s);
        return;
    }
    }
}

static void handle_client_info(struct server *s, const uint8_t *p, size_t len)
{
    memset(&s->ci, 0, sizeof(s->ci));
    memcpy(&s->ci, p, len < sizeof(s->ci) ? len : sizeof(s->ci));
    if (s->ci.version != CLIENT_INFO_CURRENT_VERSION) {
        log_info("xrdp client_info version %d, built for %d", s->ci.version,
                 CLIENT_INFO_CURRENT_VERSION);
        drop_connection(s, "incompatible xrdp");
        return;
    }
    s->have_info = true;
    int w = s->ci.display_sizes.session_width, h = s->ci.display_sizes.session_height;
    if (s->ci.display_sizes.monitorCount > 1)
        log_info("client has %u monitors; serving them as one screen",
                 s->ci.display_sizes.monitorCount);

    int interval = s->ci.capture_code == CC_GFX_A2   ? s->ci.h264_frame_interval
                   : s->ci.capture_code == CC_GFX_PRO ||
                           s->ci.capture_code == CC_SUF_RFX
                       ? s->ci.rfx_frame_interval
                       : s->ci.normal_frame_interval;
    s->interval = (interval > 0 ? interval : 16) / 1000.0;
    log_info("client %dx%d bpp %d, capture code %d, frame interval %d ms, keyboard %s/%s", w, h,
             s->ci.bpp, s->ci.capture_code, interval, s->ci.layout, s->ci.variant);
    if (s->ci.capture_code == CC_SUF_A2)
        log_info("xrdp chose non-GFX H.264, which xrdp 0.10 cannot encode; expect no picture");

    /* The client's keyboard layout, as xrdp mapped it */
    const char *layout = s->ci.layout[0] ? s->ci.layout : NULL, *variant = s->ci.variant;
    if (!layout)
        layout = rdp_layout_to_xkb(s->ci.keylayout, &variant);
    if (getenv("OMARCHY_XRDP_KEYBOARD") && *getenv("OMARCHY_XRDP_KEYBOARD")) {
        layout = getenv("OMARCHY_XRDP_KEYBOARD");
        variant = NULL;
    }
    if (layout)
        wl_set_keyboard_layout(s->wl, layout, variant && *variant ? variant : NULL);

    set_session_size(s, w, h);
    send_memory_allocation_complete(s);
    if (s->cursor)
        send_cursor(s);
}

static void handle_message(struct server *s, uint16_t type, const uint8_t *p, size_t len)
{
    switch (type) {
    case MSG_EVENT:
        handle_event(s, p, len);
        break;
    case MSG_CLIENT_INFO:
        handle_client_info(s, p, len);
        break;
    case MSG_FRAME_ACK: {
        if (len < 8)
            break;
        int32_t id;
        memcpy(&id, p + 4, 4);
        if (s->frame_ack < s->frame_id && (id == INT_MAX || id >= s->frame_id)) {
            s->ack_wait_sum += now_s() - s->sent_at;
            s->acks++;
        }
        s->frame_ack = id == INT_MAX ? s->frame_id : id;
        break;
    }
    case MSG_SUPPRESS: {
        if (len < 4)
            break;
        uint32_t suppress;
        memcpy(&suppress, p, 4);
        s->suppressed = suppress != 0;
        if (!s->suppressed) {
            s->want_full = true;
            if (s->cursor_dirty)
                send_cursor(s);
        }
        break;
    }
    }
}

/* ---- connection ---------------------------------------------------------------- */

static void drop_connection(struct server *s, const char *why)
{
    if (s->conn_fd < 0)
        return;
    log_info("xrdp disconnected (%s)", why);
    close(s->conn_fd);
    s->conn_fd = -1;
    s->have_info = false;
    s->in_len = 0;
    s->suppressed = false;
    release_all_keys(s);
}

static void accept_connection(struct server *s)
{
    int fd = accept4(s->listen_fd, NULL, NULL, SOCK_CLOEXEC | SOCK_NONBLOCK);
    if (fd < 0)
        return;
    drop_connection(s, "replaced by a new connection");
    s->conn_fd = fd;
    s->frame_id = s->frame_ack = 0;
    s->want_full = true;
    s->cursor_dirty = true;
    s->stats_start = now_s();
    s->frames_sent = 0;
    log_info("xrdp connected");
}

static void read_connection(struct server *s)
{
    for (;;) {
        if (s->in_cap - s->in_len < 65536) {
            s->in_cap = s->in_cap ? s->in_cap * 2 : 131072;
            s->in = realloc(s->in, s->in_cap);
        }
        ssize_t n = recv(s->conn_fd, s->in + s->in_len, s->in_cap - s->in_len, 0);
        if (n == 0) {
            drop_connection(s, "closed");
            return;
        }
        if (n < 0) {
            if (errno == EINTR)
                continue;
            if (errno != EAGAIN)
                drop_connection(s, strerror(errno));
            break;
        }
        s->in_len += n;
    }

    size_t off = 0;
    while (s->conn_fd >= 0 && s->in_len - off >= 6) {
        uint32_t len;
        uint16_t type;
        memcpy(&len, s->in + off, 4);
        memcpy(&type, s->in + off + 4, 2);
        if (len < 6 || len > 1 << 20) {
            drop_connection(s, "bad message length");
            return;
        }
        if (s->in_len - off < len)
            break;
        handle_message(s, type, s->in + off + 6, len - 6);
        off += len;
    }
    if (s->conn_fd >= 0) {
        memmove(s->in, s->in + off, s->in_len - off);
        s->in_len -= off;
    }
}

static int listen_on(const char *path)
{
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    struct sockaddr_un addr = {.sun_family = AF_UNIX};
    if (fd < 0 || strlen(path) >= sizeof(addr.sun_path))
        return -1;
    strcpy(addr.sun_path, path);
    unlink(path);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0 || chmod(path, 0660) < 0 ||
        listen(fd, 2) < 0) {
        log_info("cannot listen on %s: %s", path, strerror(errno));
        close(fd);
        return -1;
    }
    return fd;
}

int xup_main(const char *socket_path, const char *output, const char *resize_cmd)
{
    static struct server s;
    s.socket_path = socket_path;
    s.resize_cmd = resize_cmd;
    s.conn_fd = -1;
    s.shm_fd = -1;
    s.interval = 0.016;

    struct sigaction sa = {.sa_handler = SIG_IGN, .sa_flags = SA_NOCLDWAIT};
    sigaction(SIGCHLD, &sa, NULL);

    static const struct wl_callbacks cb = {
        .frame_ready = on_frame,
        .cursor_changed = on_cursor,
        .output_resized = on_resized,
        .stopped = on_stopped,
    };
    s.wl = wl_start(output, &cb, &s);
    if (!s.wl)
        return 1;
    /* xrdp's chansrv serves the RDP clipboard on the session's X display */
    const char *display = getenv("OMARCHY_XRDP_DISPLAY");
    struct clip *clip = display ? clip_start(s.wl, atoi(display)) : NULL;
    s.listen_fd = listen_on(socket_path);
    if (s.listen_fd < 0)
        return 1;
    log_info("listening on %s", socket_path);

    while (!s.stop) {
        int timeout = -1;
        if (ready_to_send(&s)) {
            if (s.want_full && s.last_frame && !s.capture_pending) {
                /* Repaint from the last capture: the screen may not change */
                s.want_full = false;
                send_frame(&s, s.last_frame, true);
            } else if (!s.capture_pending) {
                double wait = s.last_send + s.interval - now_s();
                if (wait <= 0) {
                    wl_request_frame(s.wl, false);
                    s.capture_pending = true;
                } else {
                    timeout = (int)(wait * 1000) + 1;
                }
            } else if (s.want_full && s.last_frame) {
                /* A capture is waiting for screen changes; repaint meanwhile */
                s.want_full = false;
                send_frame(&s, s.last_frame, true);
            }
        }
        if (s.cursor_dirty && s.cursor && s.conn_fd >= 0 && s.have_info && !s.suppressed)
            send_cursor(&s);

        /* The clipboard's X display appears after we start: keep trying */
        int x_fd = clip ? clip_x_fd(clip) : -1;
        if (clip && x_fd < 0 && (timeout < 0 || timeout > 1000))
            timeout = 1000;

        if (wl_prepare(s.wl) < 0)
            break;
        struct pollfd fds[5] = {
            {.fd = wl_fd(s.wl), .events = POLLIN},
            {.fd = s.listen_fd, .events = POLLIN},
            {.fd = s.conn_fd, .events = POLLIN},
            {.fd = x_fd, .events = POLLIN},
            {.fd = clip ? clip_read_fd(clip) : -1, .events = POLLIN},
        };
        int r = poll(fds, 5, timeout);
        if (r < 0 && errno != EINTR) {
            wl_after_poll(s.wl, false);
            break;
        }
        if (wl_after_poll(s.wl, r > 0 && (fds[0].revents & POLLIN)) < 0) {
            log_info("lost the Wayland connection");
            break;
        }
        if (r > 0 && (fds[1].revents & POLLIN))
            accept_connection(&s);
        if (r > 0 && s.conn_fd >= 0 && (fds[2].revents & (POLLIN | POLLHUP | POLLERR)))
            read_connection(&s);
        if (clip) {
            /* xcb may have queued events while we talked to it */
            if (x_fd >= 0)
                clip_dispatch_x(clip);
            if (r > 0 && fds[4].fd >= 0 && (fds[4].revents & (POLLIN | POLLHUP | POLLERR)))
                clip_dispatch_read(clip);
        }
    }
    drop_connection(&s, "shutting down");
    unlink(socket_path);
    return 0;
}
