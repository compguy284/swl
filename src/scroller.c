#include <stdlib.h>
#include <string.h>
#include <scenefx/types/wlr_scene.h>

#include "scroller.h"
#include "client.h"
#include "commands.h"
#include "layout.h"
#include "macros.h"

/* Resolve a client's scroller_cw to pixel width */
static int
resolve_width(Client *c, Monitor *m)
{
	float cw = c->scroller_cw;
	if (cw <= 0)
		cw = c->server->config.scroller_default_width;
	if (cw <= 1.0f)
		return (int)(cw * m->w.width);
	return (int)cw;
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

void
swl_scroller(SwlServer *server, Monitor *m)
{
	Client *c;
	int n = 0;

	/* Count visible tiled clients */
	wl_list_for_each(c, &server->clients, link)
		if (VISIBLEON(c, m) && !c->isfloating && !c->isfullscreen)
			n++;
	if (n == 0)
		return;

	/* Collect into temp array and compute virtual positions */
	Client **cols = calloc((size_t)n, sizeof(Client *));
	int *vx = calloc((size_t)n, sizeof(int));
	int *vw = calloc((size_t)n, sizeof(int));
	if (!cols || !vx || !vw) {
		free(cols);
		free(vx);
		free(vw);
		return;
	}

	int i = 0;
	int strip_x = m->w.x;
	wl_list_for_each(c, &server->clients, link) {
		if (!VISIBLEON(c, m) || c->isfloating || c->isfullscreen)
			continue;
		cols[i] = c;
		vw[i] = resolve_width(c, m);
		vx[i] = strip_x;
		strip_x += vw[i];
		i++;
	}

	/* Find the focused client's index */
	Client *focused = swl_focustop(server, m);
	int fi = -1;
	for (i = 0; i < n; i++) {
		if (cols[i] == focused) {
			fi = i;
			break;
		}
	}
	if (fi < 0)
		goto position; /* focused client is floating; keep current scroll_x */

	int focused_vx = vx[fi];
	int focused_vw = vw[fi];
	int focused_center = focused_vx + focused_vw / 2;
	int viewport_center = m->w.x + m->w.width / 2;

	/* Compute scroll_x based on center_focused_column mode */
	enum SwlScrollerCenter mode = server->config.scroller_center;

	/* Override: center single column if configured */
	if (n == 1 && server->config.scroller_center_single)
		mode = ScrollCenterAlways;

	switch (mode) {
	case ScrollCenterAlways:
		m->scroll_x = focused_center - viewport_center;
		break;
	case ScrollCenterOverflow:
		/* If focused column fits in viewport, use "never" logic;
		 * otherwise center it */
		if (focused_vw > m->w.width) {
			m->scroll_x = focused_center - viewport_center;
		} else {
			/* Ensure focused column is fully visible, minimal scroll */
			if (focused_vx - m->scroll_x < m->w.x)
				m->scroll_x = focused_vx - m->w.x;
			else if (focused_vx + focused_vw - m->scroll_x > m->w.x + m->w.width)
				m->scroll_x = focused_vx + focused_vw - m->w.x - m->w.width;
		}
		break;
	case ScrollCenterNever:
	default:
		/* Ensure focused column is fully visible; scroll only to nearest edge */
		if (focused_vx - m->scroll_x < m->w.x)
			m->scroll_x = focused_vx - m->w.x;
		else if (focused_vx + focused_vw - m->scroll_x > m->w.x + m->w.width)
			m->scroll_x = focused_vx + focused_vw - m->w.x - m->w.width;
		break;
	}

position:
	/* Position and clip each client */
	for (i = 0; i < n; i++) {
		c = cols[i];
		int screen_x = vx[i] - m->scroll_x;
		struct wlr_box geo = {
			.x = screen_x,
			.y = m->w.y,
			.width = vw[i],
			.height = m->w.height,
		};

		swl_resize(server, c, geo, 0);

		/* Compute visible intersection with monitor window area */
		struct wlr_box visible;
		if (!box_intersect(&visible, &c->geom, &m->w)) {
			/* Fully off-screen: disable */
			wlr_scene_node_set_enabled(&c->scene->node, false);
		} else {
			wlr_scene_node_set_enabled(&c->scene->node, true);

			if (visible.x == c->geom.x && visible.width == c->geom.width) {
				/* Fully on-screen: normal clip */
				struct wlr_box clip;
				swl_client_get_clip(c, &clip);
				wlr_scene_subsurface_tree_set_clip(&c->scene_surface->node, &clip);
			} else {
				/* Partially visible: clip surface */
				int surface_origin_x = c->geom.x + (int)c->bw;
				int surface_origin_y = c->geom.y + (int)c->bw;
				struct wlr_box sclip = {
					.x = visible.x - surface_origin_x,
					.y = visible.y - surface_origin_y,
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
			}

			/* Shadow follows visibility */
			if (c->shadow)
				wlr_scene_node_set_enabled(&c->shadow->node, true);
		}
	}

	snprintf(m->ltsymbol, LENGTH(m->ltsymbol), "[%d]|", n);

	free(cols);
	free(vx);
	free(vw);
}

void
swl_cmd_scroller_cycle_width(SwlServer *server, const Arg *arg)
{
	Client *c = swl_focustop(server, server->selmon);
	if (!c || c->isfloating || c->isfullscreen)
		return;

	SwlConfig *cfg = &server->config;
	if (cfg->scroller_preset_count == 0)
		return;

	int dir = (arg && arg->i != 0) ? arg->i : 1;
	int idx = c->scroller_preset_idx;

	if (dir > 0) {
		idx = (idx + 1) % (int)cfg->scroller_preset_count;
	} else {
		idx = (idx - 1 + (int)cfg->scroller_preset_count) % (int)cfg->scroller_preset_count;
	}

	c->scroller_preset_idx = idx;
	c->scroller_cw = cfg->scroller_preset_widths[idx];
	swl_arrange(server, server->selmon);
}

void
swl_cmd_scroller_set_width(SwlServer *server, const Arg *arg)
{
	Client *c = swl_focustop(server, server->selmon);
	if (!c || !arg || c->isfloating || c->isfullscreen)
		return;

	float w = arg->f;
	if (w <= 0)
		return;

	c->scroller_cw = w;

	/* Sync preset index if this width matches a preset, otherwise reset to 0 */
	SwlConfig *cfg = &server->config;
	c->scroller_preset_idx = 0;
	for (size_t i = 0; i < cfg->scroller_preset_count; i++) {
		if (cfg->scroller_preset_widths[i] == w) {
			c->scroller_preset_idx = (int)i;
			break;
		}
	}

	swl_arrange(server, server->selmon);
}
