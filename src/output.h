#ifndef SWL_OUTPUT_H
#define SWL_OUTPUT_H

#include "server.h"

void swl_handle_new_output(struct wl_listener *listener, void *data);
void swl_handle_layout_change(struct wl_listener *listener, void *data);
void swl_handle_output_mgr_apply(struct wl_listener *listener, void *data);
void swl_handle_output_mgr_test(struct wl_listener *listener, void *data);
void swl_output_set_power(SwlServer *server, Monitor *m, bool enabled);
void swl_handle_output_power_set_mode(struct wl_listener *listener, void *data);
void swl_handle_gpu_reset(struct wl_listener *listener, void *data);
void swl_printstatus(SwlServer *server);
Monitor *swl_dirtomon(SwlServer *server, enum wlr_direction dir);

#endif
