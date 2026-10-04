/* Wayland side: find the output, capture it with ext-image-copy-capture into
 * a memfd-backed shm buffer (which xrdp maps directly), and follow the
 * cursor through a pointer cursor session. */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>

#include "backend.h"
#include "wl_private.h"

/* ---- shm buffers ------------------------------------------------------ */

static void buffer_destroy(struct shm_buffer *b)
{
    if (b->wl_buffer)
        wl_buffer_destroy(b->wl_buffer);
    if (b->frame.data)
        munmap(b->frame.data, b->frame.size);
    if (b->frame.fd >= 0)
        close(b->frame.fd);
    memset(b, 0, sizeof(*b));
    b->frame.fd = -1;
}

static bool buffer_create(struct wl_ctx *ctx, struct shm_buffer *b, int width, int height,
                          uint32_t format)
{
    buffer_destroy(b);
    int stride = width * 4;
    size_t size = (size_t)stride * height;
    int fd = memfd_create("omarchy-xrdp-frame", MFD_CLOEXEC | MFD_ALLOW_SEALING);
    if (fd < 0 || ftruncate(fd, size) < 0) {
        log_info("cannot allocate %zu byte frame: %s", size, strerror(errno));
        if (fd >= 0)
            close(fd);
        return false;
    }
    void *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) {
        close(fd);
        return false;
    }
    struct wl_shm_pool *pool = wl_shm_create_pool(ctx->shm, fd, size);
    b->wl_buffer = wl_shm_pool_create_buffer(pool, 0, width, height, stride, format);
    wl_shm_pool_destroy(pool);

    b->frame.fd = fd;
    b->frame.data = data;
    b->frame.size = size;
    b->frame.width = width;
    b->frame.height = height;
    b->frame.stride = stride;
    b->frame.format = format;
    b->needs_full_copy = true;
    return true;
}

/* Screens prefer XRGB8888 (little-endian B,G,R,X bytes, what xrdp's 32bpp
 * paths use); cursors need ARGB8888 for their alpha */
static uint32_t pick_format(const struct capture_constraints *c, bool want_alpha)
{
    if (want_alpha && c->has_argb)
        return WL_SHM_FORMAT_ARGB8888;
    if (c->has_xrgb)
        return WL_SHM_FORMAT_XRGB8888;
    if (c->has_argb)
        return WL_SHM_FORMAT_ARGB8888;
    return UINT32_MAX;
}

/* ---- screen capture ----------------------------------------------------- */

static void screen_frame_transform(void *data, struct ext_image_copy_capture_frame_v1 *f,
                                   uint32_t transform) {}

static void screen_frame_damage(void *data, struct ext_image_copy_capture_frame_v1 *f,
                                int32_t x, int32_t y, int32_t w, int32_t h)
{
    struct wl_ctx *ctx = data;
    struct frame *fr = &ctx->screen.buffer.frame;
    if (fr->full_damage)
        return;
    if (fr->n_damage == MAX_DAMAGE_RECTS) {
        fr->full_damage = true;
        return;
    }
    fr->damage[fr->n_damage++] = (struct rect){x, y, w, h};
}

static void screen_frame_presentation_time(void *data, struct ext_image_copy_capture_frame_v1 *f,
                                           uint32_t hi, uint32_t lo, uint32_t ns) {}

static void screen_frame_ready(void *data, struct ext_image_copy_capture_frame_v1 *f)
{
    struct wl_ctx *ctx = data;
    ext_image_copy_capture_frame_v1_destroy(f);
    ctx->screen.frame = NULL;
    ctx->screen.buffer.needs_full_copy = false;

    struct frame *fr = &ctx->screen.buffer.frame;
    if (ctx->screen.send_full) {
        fr->full_damage = true;
        ctx->screen.send_full = false;
    }
    ctx->cb->frame_ready(ctx->cb_data, fr);
}

static void screen_frame_failed(void *data, struct ext_image_copy_capture_frame_v1 *f,
                                uint32_t reason)
{
    struct wl_ctx *ctx = data;
    ext_image_copy_capture_frame_v1_destroy(f);
    ctx->screen.frame = NULL;
    log_debug("screen capture failed (reason %u)", reason);
    if (reason == EXT_IMAGE_COPY_CAPTURE_FRAME_V1_FAILURE_REASON_STOPPED) {
        ctx->cb->stopped(ctx->cb_data);
        return;
    }
    /* Constraints changed (e.g. resize): the session's done event
     * reallocates; ask again so the caller still gets its frame */
    ctx->screen.retry = true;
}

static const struct ext_image_copy_capture_frame_v1_listener screen_frame_listener = {
    .transform = screen_frame_transform,
    .damage = screen_frame_damage,
    .presentation_time = screen_frame_presentation_time,
    .ready = screen_frame_ready,
    .failed = screen_frame_failed,
};

static void start_screen_frame(struct wl_ctx *ctx)
{
    struct shm_buffer *b = &ctx->screen.buffer;
    if (ctx->screen.frame || !b->wl_buffer)
        return;
    b->frame.n_damage = 0;
    b->frame.full_damage = false;
    ctx->screen.frame = ext_image_copy_capture_session_v1_create_frame(ctx->screen.session);
    ext_image_copy_capture_frame_v1_add_listener(ctx->screen.frame, &screen_frame_listener, ctx);
    ext_image_copy_capture_frame_v1_attach_buffer(ctx->screen.frame, b->wl_buffer);
    if (b->needs_full_copy)
        ext_image_copy_capture_frame_v1_damage_buffer(ctx->screen.frame, 0, 0, b->frame.width,
                                                      b->frame.height);
    ext_image_copy_capture_frame_v1_capture(ctx->screen.frame);
    ctx->screen.wanted = false;
}

static void session_buffer_size(void *data, struct ext_image_copy_capture_session_v1 *s,
                                uint32_t w, uint32_t h)
{
    struct capture *c = data;
    c->pending.width = w;
    c->pending.height = h;
}

static void session_shm_format(void *data, struct ext_image_copy_capture_session_v1 *s,
                               uint32_t format)
{
    struct capture *c = data;
    if (format == WL_SHM_FORMAT_XRGB8888)
        c->pending.has_xrgb = true;
    else if (format == WL_SHM_FORMAT_ARGB8888)
        c->pending.has_argb = true;
}

static void session_dmabuf_device(void *data, struct ext_image_copy_capture_session_v1 *s,
                                  struct wl_array *dev) {}
static void session_dmabuf_format(void *data, struct ext_image_copy_capture_session_v1 *s,
                                  uint32_t format, struct wl_array *mods) {}

static void session_done(void *data, struct ext_image_copy_capture_session_v1 *s)
{
    struct capture *c = data;
    struct wl_ctx *ctx = c->ctx;
    struct capture_constraints k = c->pending;
    memset(&c->pending, 0, sizeof(c->pending));

    uint32_t format = pick_format(&k, c != &ctx->screen);
    if (format == UINT32_MAX) {
        log_info("%s capture offers no usable shm format", c->name);
        return;
    }
    struct frame *fr = &c->buffer.frame;
    if (c->buffer.wl_buffer && fr->width == (int)k.width && fr->height == (int)k.height &&
        fr->format == format)
        return;

    log_info("%s capture %ux%u", c->name, k.width, k.height);
    if (!buffer_create(ctx, &c->buffer, k.width, k.height, format))
        return;

    if (c == &ctx->screen) {
        c->send_full = true;
        ctx->cb->output_resized(ctx->cb_data, k.width, k.height);
        if (c->wanted || c->retry) {
            c->retry = false;
            start_screen_frame(ctx);
        }
    } else {
        start_cursor_frame(ctx);
    }
}

static void session_stopped(void *data, struct ext_image_copy_capture_session_v1 *s)
{
    struct capture *c = data;
    log_info("%s capture session stopped", c->name);
    if (c == &c->ctx->screen)
        c->ctx->cb->stopped(c->ctx->cb_data);
}

const struct ext_image_copy_capture_session_v1_listener capture_session_listener = {
    .buffer_size = session_buffer_size,
    .shm_format = session_shm_format,
    .dmabuf_device = session_dmabuf_device,
    .dmabuf_format = session_dmabuf_format,
    .done = session_done,
    .stopped = session_stopped,
};

void wl_request_frame(struct wl_ctx *ctx, bool full)
{
    if (full)
        ctx->screen.send_full = true;
    ctx->screen.wanted = true;
    start_screen_frame(ctx);
}

bool wl_client_cursor(struct wl_ctx *ctx)
{
    return ctx->client_cursor;
}

void wl_get_size(struct wl_ctx *ctx, int *width, int *height)
{
    *width = ctx->screen.buffer.frame.width;
    *height = ctx->screen.buffer.frame.height;
}

/* ---- registry and output lookup ------------------------------------------ */

static void output_geometry(void *d, struct wl_output *o, int32_t x, int32_t y, int32_t pw,
                            int32_t ph, int32_t sub, const char *make, const char *model,
                            int32_t transform) {}
static void output_mode(void *d, struct wl_output *o, uint32_t flags, int32_t w, int32_t h,
                        int32_t refresh) {}
static void output_done(void *d, struct wl_output *o) {}
static void output_scale(void *d, struct wl_output *o, int32_t factor) {}
static void output_name(void *d, struct wl_output *o, const char *name)
{
    struct output_entry *e = d;
    free(e->name);
    e->name = strdup(name);
}
static void output_description(void *d, struct wl_output *o, const char *desc) {}

static const struct wl_output_listener output_listener = {
    .geometry = output_geometry,
    .mode = output_mode,
    .done = output_done,
    .scale = output_scale,
    .name = output_name,
    .description = output_description,
};

/* Console mode: the compositor reports input activity after a second of
 * idleness. Activity we didn't just inject ourselves came from the machine's
 * own keyboard or mouse. */
static double mono_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

static void idle_idled(void *data, struct ext_idle_notification_v1 *n)
{
    struct wl_ctx *ctx = data;
    ctx->idle = true;
}

static void idle_resumed(void *data, struct ext_idle_notification_v1 *n)
{
    struct wl_ctx *ctx = data;
    if (!ctx->idle)
        return;
    ctx->idle = false;
    double since = mono_now() - ctx->last_inject;
    log_debug("input activity, %.3f s after our last injected event", since);
    if (since > 0.5 && ctx->cb->local_activity)
        ctx->cb->local_activity(ctx->cb_data);
}

static const struct ext_idle_notification_v1_listener idle_listener = {
    .idled = idle_idled,
    .resumed = idle_resumed,
};

static void registry_global(void *data, struct wl_registry *reg, uint32_t name,
                            const char *iface, uint32_t version)
{
    struct wl_ctx *ctx = data;
    if (!strcmp(iface, wl_shm_interface.name)) {
        ctx->shm = wl_registry_bind(reg, name, &wl_shm_interface, 1);
    } else if (!strcmp(iface, wl_seat_interface.name) && !ctx->seat) {
        ctx->seat = wl_registry_bind(reg, name, &wl_seat_interface, version < 5 ? version : 5);
        wl_seat_add_listener(ctx->seat, &seat_listener, ctx);
    } else if (!strcmp(iface, wl_output_interface.name) && version >= 4 &&
               ctx->n_outputs < MAX_OUTPUTS) {
        struct output_entry *e = &ctx->outputs[ctx->n_outputs++];
        e->global_name = name;
        e->output = wl_registry_bind(reg, name, &wl_output_interface, 4);
        wl_output_add_listener(e->output, &output_listener, e);
    } else if (!strcmp(iface, ext_output_image_capture_source_manager_v1_interface.name)) {
        ctx->source_manager =
            wl_registry_bind(reg, name, &ext_output_image_capture_source_manager_v1_interface, 1);
    } else if (!strcmp(iface, ext_image_copy_capture_manager_v1_interface.name)) {
        ctx->copy_manager = wl_registry_bind(reg, name, &ext_image_copy_capture_manager_v1_interface, 1);
    } else if (!strcmp(iface, ext_data_control_manager_v1_interface.name)) {
        ctx->data_control_manager = wl_registry_bind(reg, name, &ext_data_control_manager_v1_interface, 1);
    } else if (!strcmp(iface, zwp_virtual_keyboard_manager_v1_interface.name)) {
        ctx->vkbd_manager = wl_registry_bind(reg, name, &zwp_virtual_keyboard_manager_v1_interface, 1);
    } else if (!strcmp(iface, zwlr_virtual_pointer_manager_v1_interface.name)) {
        ctx->vptr_manager = wl_registry_bind(reg, name, &zwlr_virtual_pointer_manager_v1_interface,
                                             version < 2 ? version : 2);
    } else if (!strcmp(iface, ext_idle_notifier_v1_interface.name) && version >= 2) {
        ctx->idle_notifier = wl_registry_bind(reg, name, &ext_idle_notifier_v1_interface, 2);
    }
}

static void registry_global_remove(void *data, struct wl_registry *reg, uint32_t name) {}

static const struct wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = registry_global_remove,
};

struct wl_ctx *wl_start(const char *output_name, const struct wl_callbacks *cb, void *cb_data)
{
    struct wl_ctx *ctx = calloc(1, sizeof(*ctx));
    ctx->cb = cb;
    ctx->cb_data = cb_data;
    ctx->screen.ctx = ctx;
    ctx->screen.name = "screen";
    ctx->screen.buffer.frame.fd = -1;
    ctx->cursor_capture.ctx = ctx;
    ctx->cursor_capture.name = "cursor";
    ctx->cursor_capture.buffer.frame.fd = -1;

    ctx->display = wl_display_connect(NULL);
    if (!ctx->display) {
        log_info("cannot connect to the Wayland display");
        free(ctx);
        return NULL;
    }
    ctx->registry = wl_display_get_registry(ctx->display);
    wl_registry_add_listener(ctx->registry, &registry_listener, ctx);
    wl_display_roundtrip(ctx->display);
    wl_display_roundtrip(ctx->display); /* output names */

    if (!ctx->shm || !ctx->source_manager || !ctx->copy_manager || !ctx->vkbd_manager ||
        !ctx->vptr_manager || !ctx->seat) {
        log_info("compositor lacks a required protocol (shm %d, capture source %d, copy capture %d, "
                 "virtual keyboard %d, virtual pointer %d, seat %d)",
                 !!ctx->shm, !!ctx->source_manager, !!ctx->copy_manager, !!ctx->vkbd_manager,
                 !!ctx->vptr_manager, !!ctx->seat);
        return NULL;
    }

    for (int i = 0; i < ctx->n_outputs; i++)
        if (ctx->outputs[i].name && !strcmp(ctx->outputs[i].name, output_name))
            ctx->output = ctx->outputs[i].output;
    if (!ctx->output) {
        log_info("no output named %s", output_name);
        return NULL;
    }

    if (!input_init(ctx))
        return NULL;

    ctx->source = ext_output_image_capture_source_manager_v1_create_source(ctx->source_manager,
                                                                           ctx->output);
    /* By default the cursor stays in the frames (Hyprland draws it there on a
     * headless output anyway): Hyprland's cursor session can't deliver its
     * themed cursors (they come back empty, or black when the capture
     * permission isn't granted). OMARCHY_XRDP_CURSOR=client tries anyway. */
    const char *mode = getenv("OMARCHY_XRDP_CURSOR");
    ctx->client_cursor = mode && !strcmp(mode, "client");
    /* The session switched Hyprland to the invisible hidden theme: the client
     * draws real images, decoded from the hotspots (cursor.c) */
    const char *theme = getenv("OMARCHY_XRDP_CURSOR_THEME");
    if (theme && *theme && !(mode && !strcmp(mode, "frames"))) {
        ctx->client_cursor = ctx->theme_cursor = true;
        const char *size = getenv("OMARCHY_XRDP_CURSOR_SIZE");
        ctx->theme_size = size && atoi(size) > 0 ? atoi(size) : 24;
        ctx->theme_shape = -1;
    }
    if (!ctx->client_cursor || !cursor_init(ctx)) {
        ctx->client_cursor = false;
        ctx->paint_cursor = true;
    }
    ctx->screen.session = ext_image_copy_capture_manager_v1_create_session(
        ctx->copy_manager, ctx->source,
        ctx->paint_cursor ? EXT_IMAGE_COPY_CAPTURE_MANAGER_V1_OPTIONS_PAINT_CURSORS : 0);
    ext_image_copy_capture_session_v1_add_listener(ctx->screen.session, &capture_session_listener,
                                                   &ctx->screen);

    /* Input notifications ignore idle inhibitors (a playing video) */
    if (cb->local_activity && ctx->idle_notifier) {
        ctx->idle_notification = ext_idle_notifier_v1_get_input_idle_notification(
            ctx->idle_notifier, 1000, ctx->seat);
        ext_idle_notification_v1_add_listener(ctx->idle_notification, &idle_listener, ctx);
    } else if (cb->local_activity) {
        log_info("compositor lacks ext-idle-notify v2, can't notice local input");
    }
    wl_display_roundtrip(ctx->display);
    return ctx;
}

int wl_fd(struct wl_ctx *ctx)
{
    return wl_display_get_fd(ctx->display);
}

int wl_prepare(struct wl_ctx *ctx)
{
    while (wl_display_prepare_read(ctx->display) != 0)
        if (wl_display_dispatch_pending(ctx->display) < 0)
            return -1;
    if (wl_display_flush(ctx->display) < 0 && errno != EAGAIN) {
        wl_display_cancel_read(ctx->display);
        return -1;
    }
    return 0;
}

int wl_after_poll(struct wl_ctx *ctx, bool readable)
{
    if (!readable) {
        wl_display_cancel_read(ctx->display);
    } else if (wl_display_read_events(ctx->display) < 0) {
        return -1;
    }
    if (wl_display_dispatch_pending(ctx->display) < 0)
        return -1;
    if (ctx->screen.retry && ctx->screen.buffer.wl_buffer && !ctx->screen.frame) {
        ctx->screen.retry = false;
        start_screen_frame(ctx);
    }
    return 0;
}
