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

	snprintf(m->ltsymbol, sizeof(m->ltsymbol), "%s", m->lt[m->sellt]->symbol);

	wl_list_for_each(c, &server->clients, link) {
		if (c->mon != m || c->scene->node.parent == server->layers[LyrFS])
			continue;
		wlr_scene_node_reparent(&c->scene->node,
			(!m->lt[m->sellt]->arrange && c->isfloating) ? server->layers[LyrTile]
			: (m->lt[m->sellt]->arrange && c->isfloating) ? server->layers[LyrFloat]
			: c->scene->node.parent);
	}

	if (m->lt[m->sellt]->arrange)
		m->lt[m->sellt]->arrange(server, m);

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
	if (!c->isfloating && !c->isfullscreen
			&& c->mon->lt[c->mon->sellt]->arrange == swl_scroller) {
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

void
swl_tile(SwlServer *server, Monitor *m)
{
	unsigned int mw, my, ty;
	int i, n = 0;
	Client *c;

	wl_list_for_each(c, &server->clients, link)
		if (VISIBLEON(c, m) && !c->isfloating && !c->isfullscreen)
			n++;
	if (n == 0)
		return;

	if (n > m->nmaster)
		mw = m->nmaster ? (int)roundf(m->w.width * m->mfact) : 0;
	else
		mw = m->w.width;

	i = my = ty = 0;
	wl_list_for_each(c, &server->clients, link) {
		if (!VISIBLEON(c, m) || c->isfloating || c->isfullscreen)
			continue;
		if (i < m->nmaster) {
			swl_resize(server, c, (struct wlr_box){.x = m->w.x,
				.y = m->w.y + my, .width = mw,
				.height = (m->w.height - my) / (MIN(n, m->nmaster) - i)}, 0);
			my += c->geom.height;
		} else {
			swl_resize(server, c, (struct wlr_box){.x = m->w.x + mw,
				.y = m->w.y + ty, .width = m->w.width - mw,
				.height = (m->w.height - ty) / (n - i)}, 0);
			ty += c->geom.height;
		}
		i++;
	}
}

void
swl_monocle(SwlServer *server, Monitor *m)
{
	Client *c;
	int n = 0;

	wl_list_for_each(c, &server->clients, link) {
		if (!VISIBLEON(c, m) || c->isfloating || c->isfullscreen)
			continue;
		swl_resize(server, c, m->w, 0);
		n++;
	}
	if (n)
		snprintf(m->ltsymbol, LENGTH(m->ltsymbol), "[%d]", n);
	if ((c = swl_focustop(server, m)))
		wlr_scene_node_raise_to_top(&c->scene->node);
}
