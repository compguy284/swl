#include <math.h>
#include <pixman.h>
#include <stdio.h>
#include <string.h>
#include <scenefx/types/wlr_scene.h>

#include "layout.h"
#include "client.h"
#include "commands.h"
#include "cursor.h"
#include "idle.h"
#include "macros.h"
#include "scroller.h"

void
swl_applybounds(Client *c, struct wlr_box *bbox)
{
	c->geom.width = MAX(1 + 2 * (int)c->bw, c->geom.width);
	c->geom.height = MAX(1 + 2 * (int)c->bw, c->geom.height);
	if (c->geom.x >= bbox->x + bbox->width)
		c->geom.x = bbox->x + bbox->width - c->geom.width;
	if (c->geom.y >= bbox->y + bbox->height)
		c->geom.y = bbox->y + bbox->height - c->geom.height;
	if (c->geom.x + c->geom.width <= bbox->x)
		c->geom.x = bbox->x;
	if (c->geom.y + c->geom.height <= bbox->y)
		c->geom.y = bbox->y;
}

void
swl_arrange(SwlServer *server, Monitor *m)
{
	Client *c;

	if (!m->wlr_output->enabled)
		return;

	wl_list_for_each(c, &server->clients, link) {
		if (c->mon == m) {
			wlr_scene_node_set_enabled(&c->scene->node, VISIBLEON(c, m));
			swl_client_set_suspended(c, !VISIBLEON(c, m));
		}
	}

	wlr_scene_node_set_enabled(&m->fullscreen_bg->node,
		(c = swl_focustop(server, m)) && c->isfullscreen);

	wl_list_for_each(c, &server->clients, link) {
		if (c->mon != m || c->scene->node.parent == server->layers[LyrFS])
			continue;
		if (c->isfloating)
			wlr_scene_node_reparent(&c->scene->node, server->layers[LyrFloat]);
	}

	swl_scroller(server, m);

	swl_motionnotify(server, 0, nullptr, 0, 0, 0, 0);
	swl_check_idle_inhibitor(server, nullptr);
}

void
swl_resize(SwlServer *server, Client *c, struct wlr_box geo, int interact)
{
	struct wlr_box *bbox;
	struct wlr_box clip;

	if (!c->mon || !swl_client_surface(c)->mapped)
		return;

	bbox = interact ? &server->sgeom : &c->mon->w;
	swl_client_set_bounds(c, geo.width, geo.height);
	c->geom = geo;
	if (!c->isfloating && !c->isfullscreen) {
		/* Scroller clients may be positioned off-screen; only enforce minimum size */
		c->geom.width = MAX(1 + 2 * (int)c->bw, c->geom.width);
		c->geom.height = MAX(1 + 2 * (int)c->bw, c->geom.height);
	} else {
		swl_applybounds(c, bbox);
	}

	/* Position the client scene node and its surface within the border */
	wlr_scene_node_set_position(&c->scene->node, c->geom.x, c->geom.y);
	wlr_scene_node_set_position(&c->scene_surface->node, c->bw, c->bw);

	/* Resize single border rect and compute hollow clip region */
	if (c->border) {
		wlr_scene_rect_set_size(c->border, c->geom.width, c->geom.height);

		if (c->bw > 0) {
			int cr = c->server->config.corner_radius;
			int inner_cr = cr > (int)c->bw ? cr - (int)c->bw : 0;
			struct clipped_region hollow = {
				.area = {
					.x = (int)c->bw,
					.y = (int)c->bw,
					.width = c->geom.width - 2 * (int)c->bw,
					.height = c->geom.height - 2 * (int)c->bw,
				},
				.corners = corner_radii_all(inner_cr),
			};
			wlr_scene_rect_set_clipped_region(c->border, hollow);
		}
	}

	if (c->shadow)
		wlr_scene_shadow_set_size(c->shadow, c->geom.width, c->geom.height);

	/* Apply the new size to the client surface */
	c->resize = swl_client_set_size(c, c->geom.width - 2 * c->bw,
		c->geom.height - 2 * c->bw);

	swl_client_get_clip(c, &clip);
	wlr_scene_subsurface_tree_set_clip(&c->scene_surface->node, &clip);
}

