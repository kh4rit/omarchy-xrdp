/* Text clipboard bridge between the session's Hyprland (ext-data-control)
 * and the X display xrdp's chansrv serves the RDP clipboard on (:N, our
 * placeholder Xwayland). Event driven on both sides: XFixes tells us when
 * chansrv takes the X CLIPBOARD (the client copied), data-control tells us
 * when a Wayland app copies. */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <xcb/xcb.h>
#include <xcb/xfixes.h>

#include "clipboard.h"
#include "wl_private.h"

#define MAX_TEXT (4 << 20)

static const char *text_mimes[] = {"text/plain;charset=utf-8", "UTF8_STRING", "text/plain", "TEXT",
                                   "STRING"};

enum { A_CLIPBOARD, A_UTF8, A_TARGETS, A_TEXT, A_STRING, A_INCR, A_PROP, A_COUNT };
static const char *atom_names[A_COUNT] = {"CLIPBOARD", "UTF8_STRING", "TARGETS", "TEXT",
                                          "STRING",    "INCR",        "OMARCHY_XRDP_CLIP"};

struct clip {
    struct wl_ctx *wl;
    int display;

    /* the text both sides currently agree on */
    char *text;
    size_t len;

    /* Wayland */
    struct ext_data_control_device_v1 *device;
    struct ext_data_control_offer_v1 *new_offer; /* being described by offer events */
    char *new_offer_mime;
    int new_offer_rank;
    struct ext_data_control_source_v1 *source; /* ours, while it is the selection */
    struct ext_data_control_offer_v1 *deferred; /* arrived while our source was set */
    char *deferred_mime;
    int read_fd; /* pipe from a Wayland offer */
    char *read_buf;
    size_t read_len;

    /* X */
    xcb_connection_t *xc;
    xcb_window_t win;
    xcb_atom_t atoms[A_COUNT];
    uint8_t xfixes_event;
    bool x_owner;
    double next_x_try;
    bool incr; /* receiving an INCR transfer */
    char *incr_buf;
    size_t incr_len;
};

static double now_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

static bool same_text(struct clip *c, const char *t, size_t len)
{
    return c->text && c->len == len && memcmp(c->text, t, len) == 0;
}

static void store_text(struct clip *c, const char *t, size_t len)
{
    free(c->text);
    c->text = malloc(len + 1);
    memcpy(c->text, t, len);
    c->text[len] = 0;
    c->len = len;
}

/* ---- X side ------------------------------------------------------------- */

static void x_take_selection(struct clip *c)
{
    if (!c->xc)
        return;
    xcb_set_selection_owner(c->xc, c->win, c->atoms[A_CLIPBOARD], XCB_CURRENT_TIME);
    xcb_flush(c->xc);
    c->x_owner = true;
}

static void x_answer_request(struct clip *c, xcb_selection_request_event_t *req)
{
    xcb_atom_t prop = req->property ? req->property : req->target;
    bool ok = false;
    if (req->target == c->atoms[A_TARGETS]) {
        xcb_atom_t targets[] = {c->atoms[A_TARGETS], c->atoms[A_UTF8], c->atoms[A_STRING],
                                c->atoms[A_TEXT]};
        xcb_change_property(c->xc, XCB_PROP_MODE_REPLACE, req->requestor, prop, XCB_ATOM_ATOM, 32,
                            4, targets);
        ok = true;
    } else if (c->text && (req->target == c->atoms[A_UTF8] || req->target == c->atoms[A_STRING] ||
                           req->target == c->atoms[A_TEXT])) {
        xcb_change_property(c->xc, XCB_PROP_MODE_REPLACE, req->requestor, prop,
                            req->target == c->atoms[A_STRING] ? c->atoms[A_STRING] : c->atoms[A_UTF8],
                            8, c->len, c->text);
        ok = true;
    }
    xcb_selection_notify_event_t ev = {
        .response_type = XCB_SELECTION_NOTIFY,
        .time = req->time,
        .requestor = req->requestor,
        .selection = req->selection,
        .target = req->target,
        .property = ok ? prop : XCB_NONE,
    };
    xcb_send_event(c->xc, 0, req->requestor, 0, (const char *)&ev);
    xcb_flush(c->xc);
}

static void wl_offer_text(struct clip *c);

static void x_got_text(struct clip *c, const char *t, size_t len)
{
    if (same_text(c, t, len))
        return;
    store_text(c, t, len);
    log_debug("clipboard: %zu bytes from the RDP client", len);
    wl_offer_text(c);
}

static void x_read_property(struct clip *c)
{
    xcb_get_property_cookie_t ck =
        xcb_get_property(c->xc, 1, c->win, c->atoms[A_PROP], XCB_GET_PROPERTY_TYPE_ANY, 0, MAX_TEXT / 4);
    xcb_get_property_reply_t *r = xcb_get_property_reply(c->xc, ck, NULL);
    if (!r)
        return;
    size_t len = xcb_get_property_value_length(r);
    const char *data = xcb_get_property_value(r);
    if (r->type == c->atoms[A_INCR]) {
        /* Large transfer: chunks arrive as property changes, empty one ends it */
        c->incr = true;
        free(c->incr_buf);
        c->incr_buf = NULL;
        c->incr_len = 0;
    } else if (c->incr) {
        if (len == 0) {
            c->incr = false;
            x_got_text(c, c->incr_buf ? c->incr_buf : "", c->incr_len);
            free(c->incr_buf);
            c->incr_buf = NULL;
        } else if (c->incr_len + len <= MAX_TEXT) {
            c->incr_buf = realloc(c->incr_buf, c->incr_len + len);
            memcpy(c->incr_buf + c->incr_len, data, len);
            c->incr_len += len;
        }
    } else {
        x_got_text(c, data, len);
    }
    free(r);
    xcb_flush(c->xc);
}

static void x_connect(struct clip *c)
{
    char name[32];
    snprintf(name, sizeof(name), ":%d", c->display);
    xcb_connection_t *xc = xcb_connect(name, NULL);
    if (xcb_connection_has_error(xc)) {
        xcb_disconnect(xc);
        c->next_x_try = now_s() + 1;
        return;
    }
    const xcb_query_extension_reply_t *ext = xcb_get_extension_data(xc, &xcb_xfixes_id);
    if (!ext || !ext->present) {
        log_info("clipboard: X display %s has no XFixes", name);
        xcb_disconnect(xc);
        c->next_x_try = now_s() + 60;
        return;
    }
    xcb_xfixes_query_version_reply_t *v =
        xcb_xfixes_query_version_reply(xc, xcb_xfixes_query_version(xc, 5, 0), NULL);
    free(v);

    xcb_intern_atom_cookie_t ck[A_COUNT];
    for (int i = 0; i < A_COUNT; i++)
        ck[i] = xcb_intern_atom(xc, 0, strlen(atom_names[i]), atom_names[i]);
    for (int i = 0; i < A_COUNT; i++) {
        xcb_intern_atom_reply_t *r = xcb_intern_atom_reply(xc, ck[i], NULL);
        c->atoms[i] = r ? r->atom : XCB_NONE;
        free(r);
    }

    xcb_screen_t *screen = xcb_setup_roots_iterator(xcb_get_setup(xc)).data;
    c->win = xcb_generate_id(xc);
    uint32_t mask = XCB_EVENT_MASK_PROPERTY_CHANGE;
    xcb_create_window(xc, XCB_COPY_FROM_PARENT, c->win, screen->root, 0, 0, 1, 1, 0,
                      XCB_WINDOW_CLASS_INPUT_ONLY, screen->root_visual, XCB_CW_EVENT_MASK, &mask);
    xcb_xfixes_select_selection_input(xc, c->win, c->atoms[A_CLIPBOARD],
                                      XCB_XFIXES_SELECTION_EVENT_MASK_SET_SELECTION_OWNER |
                                          XCB_XFIXES_SELECTION_EVENT_MASK_SELECTION_WINDOW_DESTROY |
                                          XCB_XFIXES_SELECTION_EVENT_MASK_SELECTION_CLIENT_CLOSE);
    xcb_flush(xc);
    c->xc = xc;
    c->xfixes_event = ext->first_event;
    log_info("clipboard: bridging with X display %s", name);
    /* Whatever Wayland holds already goes to the RDP client */
    if (c->text)
        x_take_selection(c);
}

int clip_x_fd(struct clip *c)
{
    if (!c->xc && now_s() >= c->next_x_try)
        x_connect(c);
    return c->xc ? xcb_get_file_descriptor(c->xc) : -1;
}

void clip_dispatch_x(struct clip *c)
{
    if (!c->xc)
        return;
    xcb_generic_event_t *ev;
    while ((ev = xcb_poll_for_event(c->xc))) {
        uint8_t type = ev->response_type & 0x7f;
        if (type == c->xfixes_event + XCB_XFIXES_SELECTION_NOTIFY) {
            xcb_xfixes_selection_notify_event_t *n = (void *)ev;
            if (n->owner != c->win && n->owner != XCB_NONE) {
                c->x_owner = false;
                xcb_convert_selection(c->xc, c->win, c->atoms[A_CLIPBOARD], c->atoms[A_UTF8],
                                      c->atoms[A_PROP], n->timestamp);
                xcb_flush(c->xc);
            }
        } else if (type == XCB_SELECTION_NOTIFY) {
            xcb_selection_notify_event_t *n = (void *)ev;
            if (n->property == XCB_NONE && n->target == c->atoms[A_UTF8]) {
                /* Owner without UTF-8: try plain STRING */
                xcb_convert_selection(c->xc, c->win, c->atoms[A_CLIPBOARD], c->atoms[A_STRING],
                                      c->atoms[A_PROP], n->time);
                xcb_flush(c->xc);
            } else if (n->property != XCB_NONE) {
                x_read_property(c);
            }
        } else if (type == XCB_PROPERTY_NOTIFY) {
            xcb_property_notify_event_t *n = (void *)ev;
            if (c->incr && n->atom == c->atoms[A_PROP] && n->state == XCB_PROPERTY_NEW_VALUE)
                x_read_property(c);
        } else if (type == XCB_SELECTION_REQUEST) {
            x_answer_request(c, (xcb_selection_request_event_t *)ev);
        } else if (type == XCB_SELECTION_CLEAR) {
            c->x_owner = false;
        }
        free(ev);
    }
    if (xcb_connection_has_error(c->xc)) {
        log_info("clipboard: lost the X display");
        xcb_disconnect(c->xc);
        c->xc = NULL;
        c->next_x_try = now_s() + 1;
    }
}

/* ---- Wayland side ------------------------------------------------------- */

static void source_send(void *data, struct ext_data_control_source_v1 *src, const char *mime,
                        int32_t fd)
{
    struct clip *c = data;
    /* Small writes block briefly at most; readers are other clients */
    int flags = fcntl(fd, F_GETFL);
    fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
    size_t off = 0;
    while (c->text && off < c->len) {
        ssize_t n = write(fd, c->text + off, c->len - off);
        if (n <= 0)
            break;
        off += n;
    }
    close(fd);
}

static void wl_read_offer(struct clip *c, struct ext_data_control_offer_v1 *offer, const char *mime);

static void source_cancelled(void *data, struct ext_data_control_source_v1 *src)
{
    struct clip *c = data;
    ext_data_control_source_v1_destroy(src);
    if (c->source == src)
        c->source = NULL;
    /* A selection that arrived while ours was current is someone else's */
    if (c->deferred) {
        struct ext_data_control_offer_v1 *o = c->deferred;
        char *mime = c->deferred_mime;
        c->deferred = NULL;
        c->deferred_mime = NULL;
        wl_read_offer(c, o, mime);
        free(mime);
    }
}

static const struct ext_data_control_source_v1_listener source_listener = {
    .send = source_send,
    .cancelled = source_cancelled,
};

static void wl_offer_text(struct clip *c)
{
    if (!c->device)
        return;
    struct ext_data_control_source_v1 *src =
        ext_data_control_manager_v1_create_data_source(c->wl->data_control_manager);
    ext_data_control_source_v1_add_listener(src, &source_listener, c);
    for (size_t i = 0; i < sizeof(text_mimes) / sizeof(text_mimes[0]); i++)
        ext_data_control_source_v1_offer(src, text_mimes[i]);
    ext_data_control_device_v1_set_selection(c->device, src);
    c->source = src;
}

static int mime_rank(const char *mime)
{
    for (size_t i = 0; i < sizeof(text_mimes) / sizeof(text_mimes[0]); i++)
        if (!strcmp(mime, text_mimes[i]))
            return 10 - (int)i;
    return 0;
}

static void offer_offer(void *data, struct ext_data_control_offer_v1 *offer, const char *mime)
{
    struct clip *c = data;
    if (offer != c->new_offer)
        return;
    int rank = mime_rank(mime);
    if (rank > c->new_offer_rank) {
        free(c->new_offer_mime);
        c->new_offer_mime = strdup(mime);
        c->new_offer_rank = rank;
    }
}

static const struct ext_data_control_offer_v1_listener offer_listener = {
    .offer = offer_offer,
};

static void device_data_offer(void *data, struct ext_data_control_device_v1 *dev,
                              struct ext_data_control_offer_v1 *offer)
{
    struct clip *c = data;
    c->new_offer = offer;
    free(c->new_offer_mime);
    c->new_offer_mime = NULL;
    c->new_offer_rank = 0;
    ext_data_control_offer_v1_add_listener(offer, &offer_listener, c);
}

static void wl_read_offer(struct clip *c, struct ext_data_control_offer_v1 *offer, const char *mime)
{
    if (!mime) {
        ext_data_control_offer_v1_destroy(offer);
        return;
    }
    int fds[2];
    if (pipe2(fds, O_CLOEXEC | O_NONBLOCK) < 0) {
        ext_data_control_offer_v1_destroy(offer);
        return;
    }
    ext_data_control_offer_v1_receive(offer, mime, fds[1]);
    close(fds[1]);
    ext_data_control_offer_v1_destroy(offer);
    if (c->read_fd >= 0)
        close(c->read_fd);
    c->read_fd = fds[0];
    free(c->read_buf);
    c->read_buf = NULL;
    c->read_len = 0;
}

static void device_selection(void *data, struct ext_data_control_device_v1 *dev,
                             struct ext_data_control_offer_v1 *offer)
{
    struct clip *c = data;
    char *mime = offer == c->new_offer ? c->new_offer_mime : NULL;
    if (offer == c->new_offer) {
        c->new_offer = NULL;
        c->new_offer_mime = NULL;
    }
    if (!offer) {
        free(mime);
        return;
    }
    if (c->source) {
        /* Most likely our own selection coming back; settle on cancel */
        if (c->deferred)
            ext_data_control_offer_v1_destroy(c->deferred);
        free(c->deferred_mime);
        c->deferred = offer;
        c->deferred_mime = mime;
        return;
    }
    wl_read_offer(c, offer, mime);
    free(mime);
}

static void device_finished(void *data, struct ext_data_control_device_v1 *dev)
{
    struct clip *c = data;
    ext_data_control_device_v1_destroy(dev);
    c->device = NULL;
}

static void device_primary_selection(void *data, struct ext_data_control_device_v1 *dev,
                                     struct ext_data_control_offer_v1 *offer)
{
    struct clip *c = data;
    if (offer == c->new_offer) {
        c->new_offer = NULL;
        free(c->new_offer_mime);
        c->new_offer_mime = NULL;
    }
    if (offer)
        ext_data_control_offer_v1_destroy(offer);
}

static const struct ext_data_control_device_v1_listener device_listener = {
    .data_offer = device_data_offer,
    .selection = device_selection,
    .finished = device_finished,
    .primary_selection = device_primary_selection,
};

int clip_read_fd(struct clip *c)
{
    return c->read_fd;
}

void clip_dispatch_read(struct clip *c)
{
    char buf[65536];
    for (;;) {
        ssize_t n = read(c->read_fd, buf, sizeof(buf));
        if (n > 0) {
            if (c->read_len + n <= MAX_TEXT) {
                c->read_buf = realloc(c->read_buf, c->read_len + n);
                memcpy(c->read_buf + c->read_len, buf, n);
                c->read_len += n;
            }
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EINTR))
            return;
        break; /* EOF or error: done */
    }
    close(c->read_fd);
    c->read_fd = -1;
    if (c->read_len > 0 && !same_text(c, c->read_buf, c->read_len)) {
        store_text(c, c->read_buf, c->read_len);
        log_debug("clipboard: %zu bytes from the session", c->read_len);
        x_take_selection(c);
    }
    free(c->read_buf);
    c->read_buf = NULL;
    c->read_len = 0;
}

struct clip *clip_start(struct wl_ctx *wl, int display)
{
    if (!wl->data_control_manager) {
        log_info("clipboard: compositor has no ext-data-control, no clipboard sync");
        return NULL;
    }
    struct clip *c = calloc(1, sizeof(*c));
    c->wl = wl;
    c->display = display;
    c->read_fd = -1;
    c->device = ext_data_control_manager_v1_get_data_device(wl->data_control_manager, wl->seat);
    ext_data_control_device_v1_add_listener(c->device, &device_listener, c);
    return c;
}
