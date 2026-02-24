#ifndef SWL_LAYER_H
#define SWL_LAYER_H

#include "server.h"

void swl_handle_new_layer_surface(struct wl_listener *listener, void *data);
void swl_arrangelayers(SwlServer *server, Monitor *m);

#endif
