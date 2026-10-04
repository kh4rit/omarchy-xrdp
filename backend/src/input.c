/* Input: a virtual keyboard (evdev keycodes interpreted with an xkb keymap
 * for the client's layout) and a virtual pointer bound to our output. */
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <xkbcommon/xkbcommon.h>

#include "wl_private.h"

static uint32_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

static bool upload_keymap(struct wl_ctx *ctx, struct xkb_keymap *keymap)
{
    char *text = xkb_keymap_get_as_string(keymap, XKB_KEYMAP_FORMAT_TEXT_V1);
    if (!text)
        return false;
    size_t size = strlen(text) + 1;
    int fd = memfd_create("omarchy-xrdp-keymap", MFD_CLOEXEC);
    bool ok = fd >= 0 && write(fd, text, size) == (ssize_t)size;
    if (ok)
        zwp_virtual_keyboard_v1_keymap(ctx->keyboard, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, fd, size);
    if (fd >= 0)
        close(fd);
    free(text);
    return ok;
}

void wl_set_keyboard_layout(struct wl_ctx *ctx, const char *layout, const char *variant)
{
    struct xkb_rule_names names = {
        .rules = NULL,
        .model = "pc105",
        .layout = layout,
        .variant = variant,
        .options = NULL,
    };
    struct xkb_keymap *keymap = xkb_keymap_new_from_names(ctx->xkb, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
    if (!keymap) {
        log_info("cannot compile xkb keymap for layout %s(%s), keeping the current one", layout,
                 variant ? variant : "");
        return;
    }
    if (!upload_keymap(ctx, keymap)) {
        xkb_keymap_unref(keymap);
        return;
    }
    if (ctx->xkb_state)
        xkb_state_unref(ctx->xkb_state);
    if (ctx->keymap)
        xkb_keymap_unref(ctx->keymap);
    ctx->keymap = keymap;
    ctx->xkb_state = xkb_state_new(keymap);
    ctx->mods_depressed = ctx->mods_latched = ctx->mods_locked = ctx->group = 0;
    zwp_virtual_keyboard_v1_modifiers(ctx->keyboard, 0, 0, 0, 0);
    log_info("keyboard layout %s%s%s", layout, variant && *variant ? "-" : "", variant ? variant : "");
}

void wl_key(struct wl_ctx *ctx, uint32_t evdev_code, bool pressed)
{
    if (!ctx->xkb_state)
        return;
    zwp_virtual_keyboard_v1_key(ctx->keyboard, now_ms(), evdev_code,
                                pressed ? WL_KEYBOARD_KEY_STATE_PRESSED : WL_KEYBOARD_KEY_STATE_RELEASED);

    /* The virtual keyboard protocol leaves modifier state to the client */
    xkb_state_update_key(ctx->xkb_state, evdev_code + 8, pressed ? XKB_KEY_DOWN : XKB_KEY_UP);
    uint32_t dep = xkb_state_serialize_mods(ctx->xkb_state, XKB_STATE_MODS_DEPRESSED);
    uint32_t lat = xkb_state_serialize_mods(ctx->xkb_state, XKB_STATE_MODS_LATCHED);
    uint32_t loc = xkb_state_serialize_mods(ctx->xkb_state, XKB_STATE_MODS_LOCKED);
    uint32_t grp = xkb_state_serialize_layout(ctx->xkb_state, XKB_STATE_LAYOUT_EFFECTIVE);
    if (dep != ctx->mods_depressed || lat != ctx->mods_latched || loc != ctx->mods_locked ||
        grp != ctx->group) {
        ctx->mods_depressed = dep;
        ctx->mods_latched = lat;
        ctx->mods_locked = loc;
        ctx->group = grp;
        zwp_virtual_keyboard_v1_modifiers(ctx->keyboard, dep, lat, loc, grp);
    }
}

void wl_get_locks(struct wl_ctx *ctx, bool *caps, bool *num)
{
    *caps = *num = false;
    if (!ctx->xkb_state)
        return;
    *caps = xkb_state_mod_name_is_active(ctx->xkb_state, XKB_MOD_NAME_CAPS,
                                         XKB_STATE_MODS_LOCKED) > 0;
    *num = xkb_state_mod_name_is_active(ctx->xkb_state, XKB_MOD_NAME_NUM,
                                        XKB_STATE_MODS_LOCKED) > 0;
}

void wl_pointer_motion(struct wl_ctx *ctx, int x, int y)
{
    int w = ctx->screen.buffer.frame.width, h = ctx->screen.buffer.frame.height;
    if (w <= 0 || h <= 0)
        return;
    if (x < 0)
        x = 0;
    if (y < 0)
        y = 0;
    if (x >= w)
        x = w - 1;
    if (y >= h)
        y = h - 1;
    zwlr_virtual_pointer_v1_motion_absolute(ctx->vpointer, now_ms(), x, y, w, h);
    zwlr_virtual_pointer_v1_frame(ctx->vpointer);
}

void wl_pointer_button(struct wl_ctx *ctx, uint32_t linux_button, bool pressed)
{
    zwlr_virtual_pointer_v1_button(ctx->vpointer, now_ms(), linux_button,
                                   pressed ? WL_POINTER_BUTTON_STATE_PRESSED
                                           : WL_POINTER_BUTTON_STATE_RELEASED);
    zwlr_virtual_pointer_v1_frame(ctx->vpointer);
}

void wl_pointer_axis(struct wl_ctx *ctx, bool horizontal, double value, int32_t discrete)
{
    uint32_t axis = horizontal ? WL_POINTER_AXIS_HORIZONTAL_SCROLL : WL_POINTER_AXIS_VERTICAL_SCROLL;
    uint32_t t = now_ms();
    zwlr_virtual_pointer_v1_axis_source(ctx->vpointer, WL_POINTER_AXIS_SOURCE_WHEEL);
    if (discrete)
        zwlr_virtual_pointer_v1_axis_discrete(ctx->vpointer, t, axis, wl_fixed_from_double(value),
                                              discrete);
    else
        zwlr_virtual_pointer_v1_axis(ctx->vpointer, t, axis, wl_fixed_from_double(value));
    zwlr_virtual_pointer_v1_frame(ctx->vpointer);
}

bool input_init(struct wl_ctx *ctx)
{
    ctx->xkb = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    ctx->keyboard = zwp_virtual_keyboard_manager_v1_create_virtual_keyboard(ctx->vkbd_manager, ctx->seat);
    wl_set_keyboard_layout(ctx, "us", NULL);
    if (!ctx->keymap)
        return false;

    if (zwlr_virtual_pointer_manager_v1_get_version(ctx->vptr_manager) >= 2)
        ctx->vpointer = zwlr_virtual_pointer_manager_v1_create_virtual_pointer_with_output(
            ctx->vptr_manager, ctx->seat, ctx->output);
    else
        ctx->vpointer = zwlr_virtual_pointer_manager_v1_create_virtual_pointer(ctx->vptr_manager, ctx->seat);
    wl_display_roundtrip(ctx->display);
    return true;
}
