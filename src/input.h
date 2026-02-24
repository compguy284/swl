#ifndef SWL_INPUT_H
#define SWL_INPUT_H

#include "server.h"

void swl_handle_new_input(struct wl_listener *listener, void *data);
void swl_handle_new_virtual_keyboard(struct wl_listener *listener, void *data);
void swl_handle_new_virtual_pointer(struct wl_listener *listener, void *data);
KeyboardGroup *swl_create_keyboard_group(SwlServer *server);
void swl_destroy_keyboard_group(struct wl_listener *listener, void *data);

#endif
