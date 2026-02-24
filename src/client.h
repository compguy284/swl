#ifndef SWL_CLIENT_H
#define SWL_CLIENT_H

#include "types.h"

int swl_client_is_x11(Client *c);
struct wlr_surface *swl_client_surface(Client *c);
int swl_toplevel_from_wlr_surface(struct wlr_surface *s, Client **pc, LayerSurface **pl);
void swl_client_activate_surface(struct wlr_surface *s, int activated);
uint32_t swl_client_set_bounds(Client *c, int32_t width, int32_t height);
const char *swl_client_get_appid(Client *c);
void swl_client_get_clip(Client *c, struct wlr_box *clip);
void swl_client_get_geometry(Client *c, struct wlr_box *geom);
Client *swl_client_get_parent(Client *c);
int swl_client_has_children(Client *c);
const char *swl_client_get_title(Client *c);
int swl_client_is_float_type(Client *c);
int swl_client_is_rendered_on_mon(Client *c, Monitor *m);
int swl_client_is_stopped(Client *c);
int swl_client_is_unmanaged(Client *c);
void swl_client_notify_enter(struct wlr_seat *seat, struct wlr_surface *s, struct wlr_keyboard *kb);
void swl_client_send_close(Client *c);
void swl_client_set_border_color(Client *c, const float color[static 4]);
void swl_client_set_fullscreen(Client *c, int fullscreen);
void swl_client_set_scale(struct wlr_surface *s, float scale);
uint32_t swl_client_set_size(Client *c, uint32_t width, uint32_t height);
void swl_client_set_tiled(Client *c, uint32_t edges);
void swl_client_set_suspended(Client *c, int suspended);
int swl_client_wants_focus(Client *c);
int swl_client_wants_fullscreen(Client *c);

#endif
