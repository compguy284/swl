#ifndef SWL_IDLE_H
#define SWL_IDLE_H

#include "server.h"

void swl_check_idle_inhibitor(SwlServer *server, struct wlr_surface *exclude);
void swl_handle_new_idle_inhibitor(struct wl_listener *listener, void *data);

#endif
