#ifndef SWL_LAYOUT_H
#define SWL_LAYOUT_H

#include "server.h"

void swl_arrange(SwlServer *server, Monitor *m);
void swl_resize(SwlServer *server, Client *c, struct wlr_box geo, int interact);
void swl_applybounds(Client *c, struct wlr_box *bbox);
void swl_clip_animated(SwlServer *server, Client *c);

#endif
