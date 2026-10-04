/* omarchy-xrdp-backend: serves a Hyprland output to xrdp over its native
 * "xup" module protocol (the one xorgxrdp speaks), so xrdp can encode the
 * session with its GFX pipeline (H.264 / RemoteFX) instead of the VNC
 * module's legacy bitmap updates. */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MAX_DAMAGE_RECTS 128

struct rect {
    int32_t x, y, w, h;
};

/* ---- Wayland side (wayland.c) ---------------------------------------- */

/* One captured frame, in a memfd-backed shm buffer xrdp can map */
struct frame {
    int fd;              /* memfd holding the pixels */
    uint8_t *data;
    int width, height;
    int stride;          /* bytes per row */
    uint32_t format;     /* wl_shm format (XRGB8888 / ARGB8888) */
    size_t size;
    struct rect damage[MAX_DAMAGE_RECTS];
    int n_damage;        /* 0 with full_damage set means whole frame */
    bool full_damage;
};

struct cursor {
    bool visible;
    int x, y;            /* position on the output, buffer coordinates */
    int hot_x, hot_y;
    int width, height;
    uint32_t *pixels;    /* ARGB8888, width*height, or NULL */
    bool shape_changed;
    bool position_changed;
};

struct wl_ctx;

struct wl_callbacks {
    void (*frame_ready)(void *data, struct frame *frame);
    void (*cursor_changed)(void *data, struct cursor *cursor);
    void (*output_resized)(void *data, int width, int height);
    void (*stopped)(void *data);
    /* console mode: input arrived that we didn't inject (may be NULL) */
    void (*local_activity)(void *data);
};

struct wl_ctx *wl_start(const char *output_name, const struct wl_callbacks *cb, void *data);
int wl_fd(struct wl_ctx *ctx);
/* Event loop integration: call wl_prepare before poll() and
 * wl_after_poll after it; both return -1 when the connection is gone */
int wl_prepare(struct wl_ctx *ctx);
int wl_after_poll(struct wl_ctx *ctx, bool readable);
/* Ask for the next frame; the compositor sends it once something changes.
 * full requests a complete frame (e.g. after a client reconnects). */
void wl_request_frame(struct wl_ctx *ctx, bool full);
void wl_get_size(struct wl_ctx *ctx, int *width, int *height);
/* Whether the client should draw the cursor (else it is in the frames) */
bool wl_client_cursor(struct wl_ctx *ctx);

/* Input injection (input.c), coordinates in output buffer pixels */
void wl_key(struct wl_ctx *ctx, uint32_t evdev_code, bool pressed);
void wl_pointer_motion(struct wl_ctx *ctx, int x, int y);
void wl_pointer_button(struct wl_ctx *ctx, uint32_t linux_button, bool pressed);
void wl_pointer_axis(struct wl_ctx *ctx, bool horizontal, double value, int32_t discrete);
/* Caps Lock / Num Lock state of the virtual keyboard */
void wl_get_locks(struct wl_ctx *ctx, bool *caps, bool *num);
/* Replace the virtual keyboard's keymap (xkb layout string like "us" or "de") */
void wl_set_keyboard_layout(struct wl_ctx *ctx, const char *layout, const char *variant);

/* ---- keymap.c --------------------------------------------------------- */

/* RDP scancode (+ extended flag) to Linux evdev keycode, 0 if unknown */
uint32_t rdp_scancode_to_evdev(int scancode, bool extended);
/* xkb layout name for a Windows keyboard layout id (e.g. 0x409 -> "us") */
const char *rdp_layout_to_xkb(uint32_t keylayout, const char **variant);

/* ---- logging ---------------------------------------------------------- */

extern int verbose;
void log_info(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void log_debug(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
