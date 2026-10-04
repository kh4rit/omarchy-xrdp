/* Cursor: a pointer cursor session reports position and hotspot, and its
 * capture session delivers the cursor image whenever it changes, so the RDP
 * client can draw the cursor itself (as with a Windows host). */
#include <stdlib.h>
#include <string.h>

#include "wl_private.h"

static void cursor_frame_transform(void *d, struct ext_image_copy_capture_frame_v1 *f, uint32_t t) {}
static void cursor_frame_damage(void *d, struct ext_image_copy_capture_frame_v1 *f, int32_t x,
                                int32_t y, int32_t w, int32_t h) {}
static void cursor_frame_presentation_time(void *d, struct ext_image_copy_capture_frame_v1 *f,
                                           uint32_t hi, uint32_t lo, uint32_t ns) {}

static void cursor_frame_ready(void *data, struct ext_image_copy_capture_frame_v1 *f)
{
    struct wl_ctx *ctx = data;
    struct capture *c = &ctx->cursor_capture;
    ext_image_copy_capture_frame_v1_destroy(f);
    c->frame = NULL;
    c->buffer.needs_full_copy = false;

    struct frame *fr = &c->buffer.frame;
    struct cursor *cur = &ctx->cursor;
    size_t n = (size_t)fr->width * fr->height;
    uint32_t *pixels = realloc(cur->pixels, n * 4);
    if (pixels) {
        for (int y = 0; y < fr->height; y++)
            memcpy(pixels + (size_t)y * fr->width, fr->data + (size_t)y * fr->stride, fr->width * 4);
        if (fr->format == WL_SHM_FORMAT_XRGB8888)
            for (size_t i = 0; i < n; i++)
                pixels[i] |= 0xff000000u;
        cur->pixels = pixels;
        cur->width = fr->width;
        cur->height = fr->height;
        cur->shape_changed = true;
        ctx->cb->cursor_changed(ctx->cb_data, cur);
        cur->shape_changed = false;
    }
    /* The next capture completes once the cursor image changes */
    start_cursor_frame(ctx);
}

static void cursor_frame_failed(void *data, struct ext_image_copy_capture_frame_v1 *f,
                                uint32_t reason)
{
    struct wl_ctx *ctx = data;
    ext_image_copy_capture_frame_v1_destroy(f);
    ctx->cursor_capture.frame = NULL;
    log_debug("cursor capture failed (reason %u)", reason);
    /* buffer_constraints: the session's done event reallocates and retries */
}

static const struct ext_image_copy_capture_frame_v1_listener cursor_frame_listener = {
    .transform = cursor_frame_transform,
    .damage = cursor_frame_damage,
    .presentation_time = cursor_frame_presentation_time,
    .ready = cursor_frame_ready,
    .failed = cursor_frame_failed,
};

void start_cursor_frame(struct wl_ctx *ctx)
{
    struct capture *c = &ctx->cursor_capture;
    if (c->frame || !c->buffer.wl_buffer)
        return;
    c->frame = ext_image_copy_capture_session_v1_create_frame(c->session);
    ext_image_copy_capture_frame_v1_add_listener(c->frame, &cursor_frame_listener, ctx);
    ext_image_copy_capture_frame_v1_attach_buffer(c->frame, c->buffer.wl_buffer);
    /* Cursor images are small: always ask for the whole buffer */
    ext_image_copy_capture_frame_v1_damage_buffer(c->frame, 0, 0, c->buffer.frame.width,
                                                  c->buffer.frame.height);
    ext_image_copy_capture_frame_v1_capture(c->frame);
}

static void cursor_enter(void *data, struct ext_image_copy_capture_cursor_session_v1 *s)
{
    struct wl_ctx *ctx = data;
    ctx->cursor.visible = true;
}

static void cursor_leave(void *data, struct ext_image_copy_capture_cursor_session_v1 *s)
{
    struct wl_ctx *ctx = data;
    ctx->cursor.visible = false;
}

static void cursor_position(void *data, struct ext_image_copy_capture_cursor_session_v1 *s,
                            int32_t x, int32_t y)
{
    struct wl_ctx *ctx = data;
    ctx->cursor.x = x;
    ctx->cursor.y = y;
}

static void cursor_hotspot(void *data, struct ext_image_copy_capture_cursor_session_v1 *s,
                           int32_t x, int32_t y)
{
    struct wl_ctx *ctx = data;
    if (ctx->cursor.hot_x == x && ctx->cursor.hot_y == y)
        return;
    ctx->cursor.hot_x = x;
    ctx->cursor.hot_y = y;
    /* A hotspot change alone (same image) still needs resending */
    if (ctx->cursor.pixels) {
        ctx->cursor.shape_changed = true;
        ctx->cb->cursor_changed(ctx->cb_data, &ctx->cursor);
        ctx->cursor.shape_changed = false;
    }
}

static const struct ext_image_copy_capture_cursor_session_v1_listener cursor_session_listener = {
    .enter = cursor_enter,
    .leave = cursor_leave,
    .position = cursor_position,
    .hotspot = cursor_hotspot,
};

static void seat_capabilities(void *data, struct wl_seat *seat, uint32_t caps)
{
    struct wl_ctx *ctx = data;
    if ((caps & WL_SEAT_CAPABILITY_POINTER) && !ctx->pointer)
        ctx->pointer = wl_seat_get_pointer(seat);
}

static void seat_name(void *data, struct wl_seat *seat, const char *name) {}

/* Added when the seat is bound, so no capabilities event is missed */
const struct wl_seat_listener seat_listener = {
    .capabilities = seat_capabilities,
    .name = seat_name,
};

bool cursor_init(struct wl_ctx *ctx)
{
    /* The seat gains a pointer once our virtual pointer exists */
    wl_display_roundtrip(ctx->display);
    if (!ctx->pointer)
        return false;

    ctx->cursor_session = ext_image_copy_capture_manager_v1_create_pointer_cursor_session(
        ctx->copy_manager, ctx->source, ctx->pointer);
    ext_image_copy_capture_cursor_session_v1_add_listener(ctx->cursor_session,
                                                          &cursor_session_listener, ctx);
    ctx->cursor_capture.session =
        ext_image_copy_capture_cursor_session_v1_get_capture_session(ctx->cursor_session);
    ext_image_copy_capture_session_v1_add_listener(ctx->cursor_capture.session,
                                                   &capture_session_listener, &ctx->cursor_capture);
    return true;
}
