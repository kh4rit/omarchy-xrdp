#pragma once

#include <wayland-client.h>

#include "ext-data-control-v1-client-protocol.h"
#include "ext-idle-notify-v1-client-protocol.h"
#include "ext-image-capture-source-v1-client-protocol.h"
#include "ext-image-copy-capture-v1-client-protocol.h"
#include "virtual-keyboard-unstable-v1-client-protocol.h"
#include "wlr-virtual-pointer-unstable-v1-client-protocol.h"

#include "backend.h"

#define MAX_OUTPUTS 16

struct shm_buffer {
    struct wl_buffer *wl_buffer;
    struct frame frame;
    bool needs_full_copy;   /* never captured into yet */
};

struct capture_constraints {
    uint32_t width, height;
    bool has_xrgb, has_argb;
};

struct capture {
    struct wl_ctx *ctx;
    const char *name;
    struct ext_image_copy_capture_session_v1 *session;
    struct ext_image_copy_capture_frame_v1 *frame;
    struct shm_buffer buffer;
    struct capture_constraints pending;
    bool wanted;     /* a frame was requested and not yet started */
    bool retry;      /* last attempt failed, try again when possible */
    bool send_full;  /* report the next frame as fully damaged */
};

struct output_entry {
    struct wl_output *output;
    uint32_t global_name;
    char *name;
};

struct wl_ctx {
    const struct wl_callbacks *cb;
    void *cb_data;

    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_shm *shm;
    struct wl_seat *seat;
    struct output_entry outputs[MAX_OUTPUTS];
    int n_outputs;
    struct wl_output *output;

    struct ext_data_control_manager_v1 *data_control_manager; /* clipboard.c */
    struct ext_output_image_capture_source_manager_v1 *source_manager;
    struct ext_image_copy_capture_manager_v1 *copy_manager;
    struct ext_image_capture_source_v1 *source;
    struct capture screen;

    /* cursor (cursor.c) */
    bool paint_cursor;  /* cursor drawn into the frames */
    bool client_cursor; /* cursor image sent for the client to draw */
    struct wl_pointer *pointer;
    struct ext_image_copy_capture_cursor_session_v1 *cursor_session;
    struct capture cursor_capture;
    struct cursor cursor;

    /* input (input.c) */
    struct zwp_virtual_keyboard_manager_v1 *vkbd_manager;
    struct zwlr_virtual_pointer_manager_v1 *vptr_manager;
    struct zwp_virtual_keyboard_v1 *keyboard;
    struct zwlr_virtual_pointer_v1 *vpointer;
    uint32_t mods_depressed, mods_latched, mods_locked, group;
    double last_inject; /* when we last injected input (monotonic s) */

    /* console mode: notice input that isn't ours (the machine's own keyboard
     * or mouse) through idle notifications */
    struct ext_idle_notifier_v1 *idle_notifier;
    struct ext_idle_notification_v1 *idle_notification;
    bool idle;
    struct xkb_context *xkb;
    struct xkb_keymap *keymap;
    struct xkb_state *xkb_state;
};

extern const struct ext_image_copy_capture_session_v1_listener capture_session_listener;
extern const struct wl_seat_listener seat_listener;

bool input_init(struct wl_ctx *ctx);
bool cursor_init(struct wl_ctx *ctx);
void start_cursor_frame(struct wl_ctx *ctx);
