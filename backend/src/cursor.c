/* Cursor: a pointer cursor session reports position and hotspot, and its
 * capture session delivers the cursor image whenever it changes, so the RDP
 * client can draw the cursor itself (as with a Windows host).
 *
 * Hyprland 0.56 never draws a cursor into those capture buffers, though.
 * In theme mode (OMARCHY_XRDP_CURSOR_THEME, the default) the session runs
 * the invisible omarchy-xrdp-hidden cursor theme, whose shapes are told
 * apart by their hotspots (see cursor-theme/generate.py); we decode the
 * shape and load its real image from the user's Xcursor theme. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <X11/Xcursor/Xcursor.h>

#include "wl_private.h"

/* Same ids as cursor-theme/generate.py: the cursor-shape-v1 order. Each
 * entry: the name, then fallbacks for themes that lack it. */
static const char *const SHAPES[][4] = {
    {NULL},
    {"default", "left_ptr"},
    {"context-menu", "left_ptr"},
    {"help", "question_arrow", "left_ptr"},
    {"pointer", "hand2", "hand1"},
    {"progress", "left_ptr_watch", "watch"},
    {"wait", "watch"},
    {"cell", "plus", "crosshair"},
    {"crosshair", "cross"},
    {"text", "xterm"},
    {"vertical-text", "text", "xterm"},
    {"alias", "dnd-link", "left_ptr"},
    {"copy", "dnd-copy", "left_ptr"},
    {"move", "fleur"},
    {"no-drop", "dnd-no-drop", "not-allowed"},
    {"not-allowed", "crossed_circle", "X_cursor"},
    {"grab", "openhand", "hand1"},
    {"grabbing", "closedhand", "fleur"},
    {"e-resize", "right_side"},
    {"n-resize", "top_side"},
    {"ne-resize", "top_right_corner"},
    {"nw-resize", "top_left_corner"},
    {"s-resize", "bottom_side"},
    {"se-resize", "bottom_right_corner"},
    {"sw-resize", "bottom_left_corner"},
    {"w-resize", "left_side"},
    {"ew-resize", "sb_h_double_arrow", "size_hor"},
    {"ns-resize", "sb_v_double_arrow", "size_ver"},
    {"nesw-resize", "size_bdiag", "fd_double_arrow"},
    {"nwse-resize", "size_fdiag", "bd_double_arrow"},
    {"col-resize", "sb_h_double_arrow"},
    {"row-resize", "sb_v_double_arrow"},
    {"all-scroll", "fleur"},
    {"zoom-in", "left_ptr"},
    {"zoom-out", "left_ptr"},
    {"dnd-ask", "left_ptr"},
    {"all-resize", "fleur"},
};
#define N_SHAPES ((int)(sizeof(SHAPES) / sizeof(SHAPES[0])))

/* Shape id from a hotspot on the 8x8 grid of a size-pixel image, 0 if the
 * hotspot isn't on it (a cursor an app drew itself) */
static int decode_shape(int hot_x, int hot_y, int size)
{
    if (size <= 0)
        return 0;
    int col = (int)lround(hot_x * 8.0 / size), row = (int)lround(hot_y * 8.0 / size);
    if (fabs(hot_x - col * size / 8.0) > 1.01 || fabs(hot_y - row * size / 8.0) > 1.01)
        return 0;
    int id = row * 8 + col;
    return id > 0 && id < N_SHAPES ? id : 0;
}

/* The RDP monitor's scale, as omarchy-xrdp-resize last set it */
static double output_scale(void)
{
    const char *runtime = getenv("XDG_RUNTIME_DIR"), *display = getenv("OMARCHY_XRDP_DISPLAY");
    double scale = 1;
    if (!runtime || !display)
        return scale;
    char path[512];
    snprintf(path, sizeof(path), "%s/omarchy-xrdp/%s/scale", runtime, display);
    FILE *f = fopen(path, "r");
    if (f) {
        if (fscanf(f, "%lf", &scale) != 1 || scale < 0.5 || scale > 4)
            scale = 1;
        fclose(f);
    }
    return scale;
}

void theme_cursor_reload(struct wl_ctx *ctx)
{
    /* NULL while wl_start is still connecting */
    if (!ctx || !ctx->theme_cursor)
        return;
    ctx->theme_shape = -1;
    theme_cursor_update(ctx);
}

void theme_cursor_update(struct wl_ctx *ctx)
{
    struct cursor *cur = &ctx->cursor;
    /* Hyprland reports the hotspot in logical pixels (image pixels divided
     * by its cursor scale), so decode against the theme's nominal size */
    int id = decode_shape(ctx->theme_hot_x, ctx->theme_hot_y, ctx->theme_size);
    if (id == ctx->theme_shape && cur->pixels)
        return;
    ctx->theme_shape = id;

    XcursorImage *img = NULL;
    const char *theme = getenv("XCURSOR_THEME");
    int size = (int)lround(ctx->theme_size * output_scale());
    for (int i = 0; id && i < 4 && SHAPES[id][i] && !img; i++)
        img = XcursorLibraryLoadImage(SHAPES[id][i], theme ? theme : "default", size);
    if (!img && id)
        img = XcursorLibraryLoadImage("left_ptr", theme ? theme : "default", size);

    if (img) {
        size_t n = (size_t)img->width * img->height;
        uint32_t *pixels = realloc(cur->pixels, n * 4);
        if (!pixels) {
            XcursorImageDestroy(img);
            return;
        }
        memcpy(pixels, img->pixels, n * 4);
        cur->pixels = pixels;
        cur->width = img->width;
        cur->height = img->height;
        cur->hot_x = img->xhot;
        cur->hot_y = img->yhot;
        cur->hidden = false;
        log_debug("cursor shape %s (%dx%d)", SHAPES[id][0], cur->width, cur->height);
        XcursorImageDestroy(img);
    } else {
        /* Not one of ours: the app draws it, and it is in the frames */
        if (!cur->pixels)
            cur->pixels = calloc(1, 4);
        cur->width = cur->height = 1;
        cur->hidden = true;
        log_debug("cursor drawn by the app (hotspot %d,%d of %d)", ctx->theme_hot_x,
                  ctx->theme_hot_y, ctx->theme_size);
    }
    cur->shape_changed = true;
    ctx->cb->cursor_changed(ctx->cb_data, cur);
    cur->shape_changed = false;
}

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
    if (ctx->theme_cursor) {
        /* Ready whenever the shape changes, with an empty image; the
         * hotspot event says which shape */
        start_cursor_frame(ctx);
        return;
    }
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
    if (ctx->theme_cursor) {
        ctx->theme_hot_x = x;
        ctx->theme_hot_y = y;
        theme_cursor_update(ctx);
        return;
    }
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
