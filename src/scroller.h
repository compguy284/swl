#ifndef SWL_SCROLLER_H
#define SWL_SCROLLER_H

#include "server.h"

void swl_scroller(SwlServer *server, Monitor *m);
void swl_cmd_scroller_cycle_width(SwlServer *server, const Arg *arg);
void swl_cmd_scroller_set_width(SwlServer *server, const Arg *arg);
void swl_cmd_consume_or_expel(SwlServer *server, const Arg *arg);
void swl_cmd_focusdir(SwlServer *server, const Arg *arg);
void swl_cmd_swapdir(SwlServer *server, const Arg *arg);

#endif
