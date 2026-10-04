#pragma once

#include "backend.h"

struct clip;

/* Bridge the session's Wayland clipboard with X display :display (where
 * xrdp's chansrv serves the RDP clipboard); NULL if unsupported */
struct clip *clip_start(struct wl_ctx *wl, int display);
/* fds to poll (-1 when none); connects to the X display when it appears */
int clip_x_fd(struct clip *c);
int clip_read_fd(struct clip *c);
void clip_dispatch_x(struct clip *c);
void clip_dispatch_read(struct clip *c);
