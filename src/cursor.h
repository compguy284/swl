#ifndef SWL_CURSOR_H
#define SWL_CURSOR_H

#include "server.h"

void swl_handle_cursor_axis(struct wl_listener *listener, void *data);
void swl_handle_cursor_button(struct wl_listener *listener, void *data);
void swl_handle_cursor_frame(struct wl_listener *listener, void *data);
void swl_handle_cursor_motion(struct wl_listener *listener, void *data);
void swl_handle_cursor_motion_absolute(struct wl_listener *listener, void *data);
void swl_handle_set_cursor(struct wl_listener *listener, void *data);
void swl_handle_set_cursor_shape(struct wl_listener *listener, void *data);
void swl_handle_new_pointer_constraint(struct wl_listener *listener, void *data);
void swl_motionnotify(SwlServer *server, uint32_t time, struct wlr_input_device *device,
		double dx, double dy, double dx_unaccel, double dy_unaccel);
void swl_pointerfocus(SwlServer *server, Client *c, struct wlr_surface *surface,
		double sx, double sy, uint32_t time);
void swl_xytonode(SwlServer *server, double x, double y, struct wlr_surface **psurface,
		Client **pc, LayerSurface **pl, double *nx, double *ny);
Monitor *swl_xytomon(SwlServer *server, double x, double y);

#endif
