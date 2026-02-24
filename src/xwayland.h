#ifndef SWL_XWAYLAND_H
#define SWL_XWAYLAND_H

#ifdef XWAYLAND

#include "server.h"

void swl_handle_new_xwayland_surface(struct wl_listener *listener, void *data);
void swl_handle_xwayland_ready(struct wl_listener *listener, void *data);

#endif /* XWAYLAND */
#endif
