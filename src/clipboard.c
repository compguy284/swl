#include <stdlib.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_primary_selection.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>

#include "clipboard.h"
#include "commands.h"
#include "cursor.h"
#include "macros.h"
#include "util.h"

static void
destroydragicon(struct wl_listener *listener, void *data)
{
	SwlListener *sl = wl_container_of(listener, sl, listener);
	SwlServer *server = sl->server;
	/* Focus enter isn't sent during drag, so refocus the focused node. */
	swl_focusclient(server, swl_focustop(server, server->selmon), 1);
	swl_motionnotify(server, 0, nullptr, 0, 0, 0, 0);
	wl_list_remove(&sl->listener.link);
	free(sl);
}

void
swl_handle_request_set_sel(struct wl_listener *listener, void *data)
{
	SwlServer *server = wl_container_of(listener, server, request_set_sel);
	struct wlr_seat_request_set_selection_event *event = data;
	wlr_seat_set_selection(server->seat, event->source, event->serial);
}

void
swl_handle_request_set_psel(struct wl_listener *listener, void *data)
{
	SwlServer *server = wl_container_of(listener, server, request_set_psel);
	struct wlr_seat_request_set_primary_selection_event *event = data;
	wlr_seat_set_primary_selection(server->seat, event->source, event->serial);
}

void
swl_handle_request_start_drag(struct wl_listener *listener, void *data)
{
	SwlServer *server = wl_container_of(listener, server, request_start_drag);
	struct wlr_seat_request_start_drag_event *event = data;

	if (wlr_seat_validate_pointer_grab_serial(server->seat, event->origin,
			event->serial))
		wlr_seat_start_pointer_drag(server->seat, event->drag, event->serial);
	else
		wlr_data_source_destroy(event->drag->source);
}

void
swl_handle_start_drag(struct wl_listener *listener, void *data)
{
	SwlServer *server = wl_container_of(listener, server, start_drag);
	struct wlr_drag *drag = data;
	if (!drag->icon)
		return;

	drag->icon->data = &wlr_scene_drag_icon_create(server->drag_icon, drag->icon)->node;
	LISTEN_STATIC(&drag->icon->events.destroy, destroydragicon, server);
}
