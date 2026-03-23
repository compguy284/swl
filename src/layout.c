#include <math.h>
#include <pixman.h>
#include <stdio.h>
#include <string.h>
#include <scenefx/types/wlr_scene.h>

#include "layout.h"
#include "animation.h"
#include "client.h"
#include "commands.h"
#include "cursor.h"
#include "idle.h"
#include "ipc.h"
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

	swl_ipc_notify_layout(server->ipc, m);
	swl_motionnotify(server, 0, nullptr, 0, 0, 0, 0);
	swl_check_idle_inhibitor(server, nullptr);
}

/* Compute intersection of two boxes; returns false if empty */
static bool
box_intersect(struct wlr_box *out, const struct wlr_box *a, const struct wlr_box *b)
{
	int x1 = MAX(a->x, b->x);
	int y1 = MAX(a->y, b->y);
	int x2 = MIN(a->x + a->width, b->x + b->width);
	int y2 = MIN(a->y + a->height, b->y + b->height);
	if (x2 <= x1 || y2 <= y1)
		return false;
	out->x = x1;
	out->y = y1;
	out->width = x2 - x1;
	out->height = y2 - y1;
	return true;
}

/* Clip a client's surface, border, and shadow to the given bounds.
 * Must be called after the client's geom, scene position, border size,
 * and surface clip have already been set up (i.e. after the main body
 * of swl_resize). */
static void
clip_to_bounds(SwlServer *server, Client *c, const struct wlr_box *bounds)
{
	struct wlr_box visible;
	if (!box_intersect(&visible, &c->geom, bounds))
		return; /* fully off-screen; the scroller handles enable/disable */

	int ox = visible.x - c->geom.x;
	int oy = visible.y - c->geom.y;

	if (ox == 0 && oy == 0
			&& visible.width == c->geom.width
			&& visible.height == c->geom.height) {
		/* Fully visible — restore default decoration state */
		if (c->border) {
			wlr_scene_node_set_position(&c->border->node, 0, 0);
			int cr = server->config.corner_radius;
			if (cr > 0)
				c->border->corners = corner_radii_all(cr);
		}
		if (c->shadow)
			wlr_scene_node_set_position(&c->shadow->node, 0, 0);
		return;
	}

	/* Clip surface */
	int so_x = c->geom.x + (int)c->bw;
	int so_y = c->geom.y + (int)c->bw;
	struct wlr_box sclip = {
		.x = visible.x - so_x,
		.y = visible.y - so_y,
		.width = visible.width,
		.height = visible.height,
	};
#ifdef XWAYLAND
	if (!swl_client_is_x11(c)) {
#endif
		sclip.x += c->surface.xdg->geometry.x;
		sclip.y += c->surface.xdg->geometry.y;
#ifdef XWAYLAND
	}
#endif
	wlr_scene_subsurface_tree_set_clip(&c->scene_surface->node, &sclip);

	/* Clip border */
	if (c->border) {
		wlr_scene_node_set_position(&c->border->node, ox, oy);
		wlr_scene_rect_set_size(c->border, visible.width, visible.height);

		if (c->bw > 0) {
			int cr = server->config.corner_radius;
			int inner_cr = cr > (int)c->bw ? cr - (int)c->bw : 0;
			bool left = (ox == 0);
			bool right = (ox + visible.width == c->geom.width);
			bool top = (oy == 0);
			bool bottom = (oy + visible.height == c->geom.height);

			int hx = (int)c->bw - ox;
			int hy = (int)c->bw - oy;
			int hx1 = MAX(0, hx);
			int hy1 = MAX(0, hy);
			int hx2 = MIN(visible.width,
				hx + c->geom.width - 2 * (int)c->bw);
			int hy2 = MIN(visible.height,
				hy + c->geom.height - 2 * (int)c->bw);

			struct clipped_region hollow = {
				.area = {
					.x = hx1, .y = hy1,
					.width = MAX(0, hx2 - hx1),
					.height = MAX(0, hy2 - hy1),
				},
				.corners = corner_radii_new(
					left && top ? inner_cr : 0,
					right && top ? inner_cr : 0,
					right && bottom ? inner_cr : 0,
					left && bottom ? inner_cr : 0),
			};
			wlr_scene_rect_set_clipped_region(c->border, hollow);

			if (cr > 0) {
				c->border->corners = corner_radii_new(
					left && top ? cr : 0,
					right && top ? cr : 0,
					right && bottom ? cr : 0,
					left && bottom ? cr : 0);
			}
		}
	}

	/* Clip shadow */
	if (c->shadow) {
		wlr_scene_node_set_position(&c->shadow->node, ox, oy);
		wlr_scene_shadow_set_size(c->shadow, visible.width, visible.height);
	}
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
	struct wlr_box old_geom = c->geom;
	c->geom = geo;
	if (!c->isfloating && !c->isfullscreen) {
		/* Scroller clients may be positioned off-screen; only enforce minimum size */
		c->geom.width = MAX(1 + 2 * (int)c->bw, c->geom.width);
		c->geom.height = MAX(1 + 2 * (int)c->bw, c->geom.height);
	} else {
		swl_applybounds(c, bbox);
	}

	/* Start move animation if geometry changed and animations are enabled.
	 * Don't animate during an open animation (let it finish first). */
	if (server->config.animations && !c->isfloating && !c->isfullscreen
			&& c->animation.action != AnimOpen
			&& (old_geom.x != c->geom.x || old_geom.y != c->geom.y
				|| old_geom.width != c->geom.width || old_geom.height != c->geom.height)
			&& old_geom.width > 0) {
		struct wlr_box start = c->animation.running
			? c->animation.current : old_geom;
		swl_animation_start(&c->animation, AnimMove,
			server->config.anim_duration_move, &start, &c->geom);
		swl_request_frame_all(server);
	}

	/* Position the client scene node and its surface within the border.
	 * If an animation is running, rendermon() handles position updates. */
	if (c->animation.running && server->config.animations) {
		/* Don't snap position — let the animation drive it */
	} else {
		wlr_scene_node_set_position(&c->scene->node, c->geom.x, c->geom.y);
	}
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

	/* Clip tiled clients to monitor bounds so they never bleed onto
	 * adjacent monitors.  This is applied here (rather than only in
	 * the scroller) so that the clip survives commitnotify calls that
	 * re-enter swl_resize outside the scroller path. */
	if (!c->isfloating && !c->isfullscreen)
		clip_to_bounds(server, c, &c->mon->w);
}

void
swl_clip_animated(SwlServer *server, Client *c)
{
	/* During animation, clip using the animated position instead of c->geom.
	 * We temporarily swap in animation.current, clip, then restore. */
	if (!c->mon)
		return;
	struct wlr_box saved = c->geom;
	if (c->animation.running)
		c->geom = c->animation.current;
	clip_to_bounds(server, c, &c->mon->w);
	c->geom = saved;
}

