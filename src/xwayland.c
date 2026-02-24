/*
 * XWayland support — extracted from swl.c.
 *
 * Handles XWayland surface creation, configuration, association/dissociation,
 * activation, hints, and the XWayland ready event.
 */
#ifdef XWAYLAND

#include <wayland-server-core.h>
#include <wlr/xwayland.h>
#include <xcb/xcb_icccm.h>

#include "xwayland.h"
#include "client.h"
#include "commands.h"
#include "layout.h"
#include "output.h"
#include "macros.h"
#include "util.h"

/* static forward declarations */
static void activatex11(struct wl_listener *listener, void *data);
static void associatex11(struct wl_listener *listener, void *data);
static void dissociatex11(struct wl_listener *listener, void *data);
static void configurex11(struct wl_listener *listener, void *data);
static void sethints(struct wl_listener *listener, void *data);

void
swl_handle_new_xwayland_surface(struct wl_listener *listener, void *data)
{
	SwlServer *server = wl_container_of(listener, server, new_xwayland_surface);
	struct wlr_xwayland_surface *xsurface = data;
	Client *c;

	/* Allocate a Client for this surface */
	c = xsurface->data = ecalloc(1, sizeof(*c));
	c->surface.xwayland = xsurface;
	c->type = X11;
	c->server = server;
	c->bw = swl_client_is_unmanaged(c) ? 0 : server->config.borderpx;

	/* Listen to the various events it can emit */
	LISTEN(&xsurface->events.associate, &c->associate, associatex11);
	LISTEN(&xsurface->events.destroy, &c->destroy, swl_handle_destroy);
	LISTEN(&xsurface->events.dissociate, &c->dissociate, dissociatex11);
	LISTEN(&xsurface->events.request_activate, &c->activate, activatex11);
	LISTEN(&xsurface->events.request_configure, &c->configure, configurex11);
	LISTEN(&xsurface->events.request_fullscreen, &c->fullscreen, swl_handle_fullscreen);
	LISTEN(&xsurface->events.set_hints, &c->set_hints, sethints);
	LISTEN(&xsurface->events.set_title, &c->set_title, swl_handle_update_title);
}

void
swl_handle_xwayland_ready(struct wl_listener *listener, void *data)
{
	SwlServer *server = wl_container_of(listener, server, xwayland_ready);
	struct wlr_xcursor *xcursor;

	/* Assign the one and only seat */
	wlr_xwayland_set_seat(server->xwayland, server->seat);

	/* Set the default XWayland cursor to match the rest of swl. */
	if ((xcursor = wlr_xcursor_manager_get_xcursor(server->cursor_mgr, "default", 1)))
		wlr_xwayland_set_cursor(server->xwayland,
				xcursor->images[0]->buffer, xcursor->images[0]->width * 4,
				xcursor->images[0]->width, xcursor->images[0]->height,
				xcursor->images[0]->hotspot_x, xcursor->images[0]->hotspot_y);
}

static void
activatex11(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, activate);

	/* Only "managed" windows can be activated */
	if (!swl_client_is_unmanaged(c))
		wlr_xwayland_surface_activate(c->surface.xwayland, 1);
}

static void
associatex11(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, associate);

	LISTEN(&swl_client_surface(c)->events.map, &c->map, swl_handle_map);
	LISTEN(&swl_client_surface(c)->events.unmap, &c->unmap, swl_handle_unmap);
}

static void
dissociatex11(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, dissociate);
	wl_list_remove(&c->map.link);
	wl_list_remove(&c->unmap.link);
}

static void
configurex11(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, configure);
	SwlServer *server = c->server;
	struct wlr_xwayland_surface_configure_event *event = data;

	if (!swl_client_surface(c) || !swl_client_surface(c)->mapped) {
		wlr_xwayland_surface_configure(c->surface.xwayland,
				event->x, event->y, event->width, event->height);
		return;
	}
	if (swl_client_is_unmanaged(c)) {
		wlr_scene_node_set_position(&c->scene->node, event->x, event->y);
		wlr_xwayland_surface_configure(c->surface.xwayland,
				event->x, event->y, event->width, event->height);
		return;
	}
	if (c->isfloating && c != server->grabc) {
		swl_resize(server, c, (struct wlr_box){.x = event->x - c->bw,
				.y = event->y - c->bw, .width = event->width + c->bw * 2,
				.height = event->height + c->bw * 2}, 0);
	} else {
		swl_arrange(server, c->mon);
	}
}

static void
sethints(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, set_hints);
	SwlServer *server = c->server;
	struct wlr_surface *surface = swl_client_surface(c);

	if (c == swl_focustop(server, server->selmon) || !c->surface.xwayland->hints)
		return;

	c->isurgent = xcb_icccm_wm_hints_get_urgency(c->surface.xwayland->hints);
	swl_printstatus(server);

	if (c->isurgent && surface && surface->mapped)
		swl_client_set_border_color(c, server->config.urgentcolor);
}

#endif /* XWAYLAND */
