#ifndef SWL_SCROLLER_H
#define SWL_SCROLLER_H

#include "server.h"

void swl_scroller(SwlServer *server, Monitor *m);
void swl_cmd_scroller_cycle_width(SwlServer *server, const Arg *arg);
void swl_cmd_scroller_set_width(SwlServer *server, const Arg *arg);

#endif
