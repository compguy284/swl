#include <stdlib.h>
#include <wlr/types/wlr_idle_inhibit_v1.h>
#include <wlr/types/wlr_idle_notify_v1.h>
#include <wlr/types/wlr_scene.h>

#include "idle.h"
#include "macros.h"
#include "util.h"

static void
destroyidleinhibitor(struct wl_listener *listener, void *data)
{
	SwlListener *sl = wl_container_of(listener, sl, listener);
	/* `data` is the wlr_surface of the idle inhibitor being destroyed,
	 * at this point the idle inhibitor is still in the list of the manager */
	swl_check_idle_inhibitor(sl->server, wlr_surface_get_root_surface(data));
	wl_list_remove(&sl->listener.link);
	free(sl);
}

void
swl_check_idle_inhibitor(SwlServer *server, struct wlr_surface *exclude)
{
	int inhibited = 0, unused_lx, unused_ly;
	struct wlr_idle_inhibitor_v1 *inhibitor;
	wl_list_for_each(inhibitor, &server->idle_inhibit_mgr->inhibitors, link) {
		struct wlr_surface *surface = wlr_surface_get_root_surface(inhibitor->surface);
		struct wlr_scene_tree *tree = surface->data;
		if (exclude != surface && (server->config.bypass_surface_visibility || (!tree
				|| wlr_scene_node_coords(&tree->node, &unused_lx, &unused_ly)))) {
			inhibited = 1;
			break;
		}
	}

	wlr_idle_notifier_v1_set_inhibited(server->idle_notifier, inhibited);
}

void
swl_handle_new_idle_inhibitor(struct wl_listener *listener, void *data)
{
	SwlServer *server = wl_container_of(listener, server, new_idle_inhibitor);
	struct wlr_idle_inhibitor_v1 *idle_inhibitor = data;

	LISTEN_STATIC(&idle_inhibitor->events.destroy, destroyidleinhibitor, server);

	swl_check_idle_inhibitor(server, nullptr);
}
