#ifndef SWL_CLIPBOARD_H
#define SWL_CLIPBOARD_H

#include "server.h"

void swl_handle_request_set_sel(struct wl_listener *listener, void *data);
void swl_handle_request_set_psel(struct wl_listener *listener, void *data);
void swl_handle_request_start_drag(struct wl_listener *listener, void *data);
void swl_handle_start_drag(struct wl_listener *listener, void *data);

#endif
