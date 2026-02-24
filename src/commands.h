#ifndef SWL_COMMANDS_H
#define SWL_COMMANDS_H

#include "server.h"

void swl_cmd_spawn(SwlServer *server, const Arg *arg);
void swl_cmd_killclient(SwlServer *server, const Arg *arg);
void swl_cmd_chvt(SwlServer *server, const Arg *arg);
void swl_cmd_quit(SwlServer *server, const Arg *arg);
void swl_cmd_focusstack(SwlServer *server, const Arg *arg);
void swl_cmd_focusmon(SwlServer *server, const Arg *arg);
void swl_cmd_setmfact(SwlServer *server, const Arg *arg);
void swl_cmd_incnmaster(SwlServer *server, const Arg *arg);
void swl_cmd_setlayout(SwlServer *server, const Arg *arg);
void swl_cmd_togglefloating(SwlServer *server, const Arg *arg);
void swl_cmd_togglefullscreen(SwlServer *server, const Arg *arg);
void swl_cmd_view(SwlServer *server, const Arg *arg);
void swl_cmd_tag(SwlServer *server, const Arg *arg);
void swl_cmd_tagmon(SwlServer *server, const Arg *arg);
void swl_cmd_toggletag(SwlServer *server, const Arg *arg);
void swl_cmd_toggleview(SwlServer *server, const Arg *arg);
void swl_cmd_zoom(SwlServer *server, const Arg *arg);
void swl_cmd_moveresize(SwlServer *server, const Arg *arg);

void swl_focusclient(SwlServer *server, Client *c, int lift);
Client *swl_focustop(SwlServer *server, Monitor *m);
void swl_setmon(SwlServer *server, Client *c, Monitor *m, uint32_t newtags);
void swl_setfloating(SwlServer *server, Client *c, int floating);
void swl_setfullscreen(SwlServer *server, Client *c, int fullscreen);
void swl_applyrules(SwlServer *server, Client *c);

void swl_commands_set_server(SwlServer *s);
void swl_commands_setup_listeners(SwlServer *server);
void swl_handlesig(int signo);

/* Internal listener callbacks needed by xwayland module */
void swl_handle_map(struct wl_listener *listener, void *data);
void swl_handle_unmap(struct wl_listener *listener, void *data);
void swl_handle_destroy(struct wl_listener *listener, void *data);
void swl_handle_fullscreen(struct wl_listener *listener, void *data);
void swl_handle_update_title(struct wl_listener *listener, void *data);

#endif
