#include <stdlib.h>
#include <string.h>
#include <scenefx/types/wlr_scene.h>

#include "scroller.h"
#include "client.h"
#include "commands.h"
#include "layout.h"
#include "macros.h"

#define IS_TILED_ON(c, m) (VISIBLEON(c, m) && !(c)->isfloating && !(c)->isfullscreen)

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

/* ===== Column helper functions ===== */

/* Walk backward through visible tiled clients to find the column head */
static Client *
find_column_head(SwlServer *server, Client *c, Monitor *m)
{
	if (!c->scroller_continuation)
		return c;
	Client *head = c;
	Client *iter;
	wl_list_for_each_reverse(iter, &c->link, link) {
		if (&iter->link == &server->clients)
			break;
		if (!IS_TILED_ON(iter, m))
			continue;
		if (!iter->scroller_continuation)
			return iter;
		head = iter;
	}
	return head;
}

/* Walk forward from head through continuation clients, return the last member */
static Client *
find_last_column_member(SwlServer *server, Client *head, Monitor *m)
{
	Client *last = head;
	Client *iter;
	wl_list_for_each(iter, &head->link, link) {
		if (&iter->link == &server->clients)
			break;
		if (!IS_TILED_ON(iter, m))
			continue;
		if (!iter->scroller_continuation)
			break;
		last = iter;
	}
	return last;
}

/* Find the head of the adjacent column (dir: -1 = left, +1 = right) */
static Client *
find_adjacent_column_head(SwlServer *server, Client *col_head, Monitor *m, int dir)
{
	if (dir > 0) {
		/* Walk forward past current column members, then find next head */
		Client *last = find_last_column_member(server, col_head, m);
		Client *iter;
		wl_list_for_each(iter, &last->link, link) {
			if (&iter->link == &server->clients)
				break;
			if (!IS_TILED_ON(iter, m))
				continue;
			/* This must be the next column head (not a continuation
			 * of ours since we started after last) */
			return iter;
		}
	} else {
		/* Walk backward from col_head to find the previous column's head */
		Client *prev_member = nullptr;
		Client *iter;
		wl_list_for_each_reverse(iter, &col_head->link, link) {
			if (&iter->link == &server->clients)
				break;
			if (!IS_TILED_ON(iter, m))
				continue;
			prev_member = iter;
			break;
		}
		if (prev_member)
			return find_column_head(server, prev_member, m);
	}
	return nullptr;
}

/* Count visible tiled members in the column starting from head */
static int
column_member_count(SwlServer *server, Client *head, Monitor *m)
{
	int count = 1;
	Client *iter;
	wl_list_for_each(iter, &head->link, link) {
		if (&iter->link == &server->clients)
			break;
		if (!IS_TILED_ON(iter, m))
			continue;
		if (!iter->scroller_continuation)
			break;
		count++;
	}
	return count;
}

/* Return 0-based index of client within its column */
static int
member_index(SwlServer *server, Client *head, Client *c, Monitor *m)
{
	if (c == head)
		return 0;
	int idx = 0;
	Client *iter;
	wl_list_for_each(iter, &head->link, link) {
		if (&iter->link == &server->clients)
			break;
		if (!IS_TILED_ON(iter, m))
			continue;
		if (!iter->scroller_continuation)
			break;
		idx++;
		if (iter == c)
			return idx;
	}
	return idx;
}

/* Return the nth member (0-based) of a column, clamped to last */
static Client *
nth_column_member(SwlServer *server, Client *head, Monitor *m, int n)
{
	if (n == 0)
		return head;
	int idx = 0;
	Client *last = head;
	Client *iter;
	wl_list_for_each(iter, &head->link, link) {
		if (&iter->link == &server->clients)
			break;
		if (!IS_TILED_ON(iter, m))
			continue;
		if (!iter->scroller_continuation)
			break;
		idx++;
		last = iter;
		if (idx == n)
			return iter;
	}
	return last; /* clamp to last member */
}

/* ===== focusdir command ===== */

void
swl_cmd_focusdir(SwlServer *server, const Arg *arg)
{
	Client *sel = swl_focustop(server, server->selmon);
	if (!sel || sel->isfloating || sel->isfullscreen)
		return;

	Monitor *m = server->selmon;
	Client *head = find_column_head(server, sel, m);
	Client *target = nullptr;
	int dir = arg->i; /* 0=left, 1=right, 2=up, 3=down */

	if (dir <= 1) {
		/* Left/Right: adjacent column, same vertical index */
		int hdir = (dir == 0) ? -1 : 1;
		Client *adj = find_adjacent_column_head(server, head, m, hdir);
		if (!adj)
			return;
		int idx = member_index(server, head, sel, m);
		target = nth_column_member(server, adj, m, idx);
	} else if (dir == 2) {
		/* Up: previous member in column */
		if (sel == head)
			return;
		Client *iter;
		wl_list_for_each_reverse(iter, &sel->link, link) {
			if (&iter->link == &server->clients)
				break;
			if (IS_TILED_ON(iter, m)) {
				target = iter;
				break;
			}
		}
	} else {
		/* Down: next member in column */
		Client *iter;
		wl_list_for_each(iter, &sel->link, link) {
			if (&iter->link == &server->clients)
				break;
			if (!IS_TILED_ON(iter, m))
				continue;
			if (!iter->scroller_continuation)
				break;
			target = iter;
			break;
		}
	}

	if (target) {
		swl_focusclient(server, target, 1);
		swl_arrange(server, server->selmon);
	}
}

/* ===== swapdir command ===== */

/* Collect all tiled members of a column into buf[]. Returns count. */
static int
collect_column_members(SwlServer *server, Client *head, Monitor *m,
                       Client **buf, int max)
{
	int n = 0;
	if (n < max)
		buf[n++] = head;
	Client *iter;
	wl_list_for_each(iter, &head->link, link) {
		if (&iter->link == &server->clients)
			break;
		if (!IS_TILED_ON(iter, m))
			continue;
		if (!iter->scroller_continuation)
			break;
		if (n < max)
			buf[n++] = iter;
	}
	return n;
}

void
swl_cmd_swapdir(SwlServer *server, const Arg *arg)
{
	Client *sel = swl_focustop(server, server->selmon);
	if (!sel || sel->isfloating || sel->isfullscreen)
		return;

	Monitor *m = server->selmon;
	Client *head = find_column_head(server, sel, m);
	int dir = arg->i; /* 0=left, 1=right, 2=up, 3=down */

	if (dir <= 1) {
		/* Left/Right: swap entire columns */
		int hdir = (dir == 0) ? -1 : 1;
		Client *adj_head = find_adjacent_column_head(server, head, m, hdir);
		if (!adj_head)
			return;

		int adj_count = column_member_count(server, adj_head, m);
		Client *adj_members[64];
		int n = collect_column_members(server, adj_head, m, adj_members, 64);
		if (n != adj_count || n == 0)
			return;

		/* Remove all adjacent column members from the list */
		for (int i = 0; i < n; i++)
			wl_list_remove(&adj_members[i]->link);

		if (dir == 1) {
			/* Swap right: move right column to before current column's head */
			struct wl_list *insert_point = head->link.prev;
			for (int i = 0; i < n; i++) {
				wl_list_insert(insert_point, &adj_members[i]->link);
				insert_point = &adj_members[i]->link;
			}
		} else {
			/* Swap left: move left column to after current column's last member */
			Client *last = find_last_column_member(server, head, m);
			struct wl_list *insert_point = &last->link;
			for (int i = 0; i < n; i++) {
				wl_list_insert(insert_point, &adj_members[i]->link);
				insert_point = &adj_members[i]->link;
			}
		}
	} else if (dir == 2) {
		/* Up: swap with previous member in column */
		if (sel == head)
			return;

		/* Find the previous tiled member */
		Client *target = nullptr;
		Client *iter;
		wl_list_for_each_reverse(iter, &sel->link, link) {
			if (&iter->link == &server->clients)
				break;
			if (IS_TILED_ON(iter, m)) {
				target = iter;
				break;
			}
		}
		if (!target)
			return;

		/* Remove sel and insert before target */
		wl_list_remove(&sel->link);
		wl_list_insert(target->link.prev, &sel->link);

		/* If target was the column head, transfer head properties to sel */
		if (target == head) {
			sel->scroller_continuation = false;
			sel->scroller_cw = target->scroller_cw;
			sel->scroller_preset_idx = target->scroller_preset_idx;
			target->scroller_continuation = true;
		}
	} else {
		/* Down: swap with next continuation member in column */
		Client *target = nullptr;
		Client *iter;
		wl_list_for_each(iter, &sel->link, link) {
			if (&iter->link == &server->clients)
				break;
			if (!IS_TILED_ON(iter, m))
				continue;
			if (!iter->scroller_continuation)
				break;
			target = iter;
			break;
		}
		if (!target)
			return;

		/* Remove sel and insert after target */
		wl_list_remove(&sel->link);
		wl_list_insert(&target->link, &sel->link);

		/* If sel was the column head, transfer head properties to target */
		if (sel == head) {
			target->scroller_continuation = false;
			target->scroller_cw = sel->scroller_cw;
			target->scroller_preset_idx = sel->scroller_preset_idx;
			sel->scroller_continuation = true;
		}
	}

	swl_arrange(server, m);
}

/* ===== consume_or_expel command ===== */

void
swl_cmd_consume_or_expel(SwlServer *server, const Arg *arg)
{
	if (!arg)
		return;
	int dir = arg->i; /* -1 = left, +1 = right */
	Monitor *m = server->selmon;
	Client *focused = swl_focustop(server, m);
	if (!focused || focused->isfloating || focused->isfullscreen)
		return;

	Client *head = find_column_head(server, focused, m);
	int count = column_member_count(server, head, m);

	if (count == 1) {
		/* Consume: take a client from the adjacent column */
		Client *adj_head = find_adjacent_column_head(server, head, m, dir);
		if (!adj_head)
			return;

		/* Pick which client to take:
		 * consuming from left (-1) → take last member of left column
		 * consuming from right (+1) → take head of right column */
		Client *target;
		if (dir < 0)
			target = find_last_column_member(server, adj_head, m);
		else
			target = adj_head;

		/* If target is a column head with continuations, promote next */
		if (!target->scroller_continuation) {
			Client *next;
			wl_list_for_each(next, &target->link, link) {
				if (&next->link == &server->clients)
					break;
				if (!IS_TILED_ON(next, m))
					continue;
				if (next->scroller_continuation) {
					next->scroller_continuation = false;
					next->scroller_cw = target->scroller_cw;
					next->scroller_preset_idx = target->scroller_preset_idx;
				}
				break;
			}
		}

		/* Move target into focused column: insert after last member */
		Client *last = find_last_column_member(server, head, m);
		wl_list_remove(&target->link);
		wl_list_insert(&last->link, &target->link);
		target->scroller_continuation = true;
	} else {
		/* Expel: remove focused from the stack as its own column */

		/* Find a reference client that remains in the column */
		Client *ref;
		if (focused == head) {
			/* Promote the next continuation to head */
			ref = nullptr;
			Client *iter;
			wl_list_for_each(iter, &focused->link, link) {
				if (&iter->link == &server->clients)
					break;
				if (!IS_TILED_ON(iter, m))
					continue;
				if (iter->scroller_continuation) {
					iter->scroller_continuation = false;
					iter->scroller_cw = focused->scroller_cw;
					iter->scroller_preset_idx = focused->scroller_preset_idx;
					ref = iter;
				}
				break;
			}
		} else {
			ref = head;
		}

		/* Remove focused from the list */
		wl_list_remove(&focused->link);

		/* Insert based on direction relative to the remaining column */
		if (ref) {
			Client *remaining_head = find_column_head(server, ref, m);
			Client *remaining_last = find_last_column_member(server, remaining_head, m);
			if (dir > 0)
				wl_list_insert(&remaining_last->link, &focused->link);
			else
				wl_list_insert(remaining_head->link.prev, &focused->link);
		} else {
			wl_list_insert(server->clients.prev, &focused->link);
		}

		focused->scroller_continuation = false;
		focused->scroller_cw = 0;
		focused->scroller_preset_idx = 0;
	}

	swl_arrange(server, m);
}

/* ===== Layout function ===== */

void
swl_scroller(SwlServer *server, Monitor *m)
{
	Client *c;
	int total = 0, ncols = 0;

	/* First pass: count visible tiled clients and columns */
	wl_list_for_each(c, &server->clients, link) {
		if (!IS_TILED_ON(c, m))
			continue;
		total++;
		if (!c->scroller_continuation)
			ncols++;
	}
	if (total == 0)
		return;

	/* Allocate arrays */
	Client **all = calloc((size_t)total, sizeof(Client *));
	int *col_start = calloc((size_t)ncols, sizeof(int)); /* index into all[] */
	int *col_count = calloc((size_t)ncols, sizeof(int));
	int *vx = calloc((size_t)ncols, sizeof(int));
	int *vw = calloc((size_t)ncols, sizeof(int));
	if (!all || !col_start || !col_count || !vx || !vw) {
		free(all);
		free(col_start);
		free(col_count);
		free(vx);
		free(vw);
		return;
	}

	/* Second pass: populate arrays */
	int gap = server->config.gap_width;
	int ci = 0, col_idx = -1;
	int strip_x = m->w.x + gap;
	wl_list_for_each(c, &server->clients, link) {
		if (!IS_TILED_ON(c, m))
			continue;
		all[ci] = c;
		if (!c->scroller_continuation) {
			col_idx++;
			col_start[col_idx] = ci;
			col_count[col_idx] = 1;
			vw[col_idx] = resolve_width(c, m);
			vx[col_idx] = strip_x;
			strip_x += vw[col_idx] + gap;
		} else {
			col_count[col_idx]++;
		}
		ci++;
	}

	/* Find the focused client's column index */
	Client *focused = swl_focustop(server, m);
	int fi = -1;
	for (int i = 0; i < total; i++) {
		if (all[i] == focused) {
			/* Find which column this client belongs to */
			for (int j = 0; j < ncols; j++) {
				if (i >= col_start[j] && i < col_start[j] + col_count[j]) {
					fi = j;
					break;
				}
			}
			break;
		}
	}
	if (fi < 0)
		goto position; /* focused is floating; keep current scroll_x */

	int focused_vx = vx[fi];
	int focused_vw = vw[fi];
	int focused_center = focused_vx + focused_vw / 2;
	int viewport_center = m->w.x + m->w.width / 2;

	/* Compute scroll_x based on center_focused_column mode */
	enum SwlScrollerCenter mode = server->config.scroller_center;

	if (ncols == 1 && server->config.scroller_center_single)
		mode = ScrollCenterAlways;

	switch (mode) {
	case ScrollCenterAlways:
		m->scroll_x = focused_center - viewport_center;
		break;
	case ScrollCenterOverflow:
		if (focused_vw > m->w.width) {
			m->scroll_x = focused_center - viewport_center;
		} else {
			if (focused_vx - gap - m->scroll_x < m->w.x)
				m->scroll_x = focused_vx - gap - m->w.x;
			else if (focused_vx + focused_vw + gap - m->scroll_x > m->w.x + m->w.width)
				m->scroll_x = focused_vx + focused_vw + gap - m->w.x - m->w.width;
		}
		break;
	case ScrollCenterNever:
	default:
		if (focused_vx - gap - m->scroll_x < m->w.x)
			m->scroll_x = focused_vx - gap - m->w.x;
		else if (focused_vx + focused_vw + gap - m->scroll_x > m->w.x + m->w.width)
			m->scroll_x = focused_vx + focused_vw + gap - m->w.x - m->w.width;
		break;
	}

position:
	/* Position and clip each client */
	for (int i = 0; i < ncols; i++) {
		int screen_x = vx[i] - m->scroll_x;
		int members = col_count[i];

		int total_vgap = gap * (members + 1);
		int avail_h = m->w.height - total_vgap;
		int slot_h = avail_h / members;

		for (int j = 0; j < members; j++) {
			c = all[col_start[i] + j];
			int y_pos = m->w.y + gap + j * (slot_h + gap);
			int h = (j == members - 1)
				? (m->w.y + m->w.height - gap - y_pos)
				: slot_h;

			struct wlr_box geo = {
				.x = screen_x,
				.y = y_pos,
				.width = vw[i],
				.height = h,
			};

			swl_resize(server, c, geo, 0);

			/* Compute visible intersection with monitor window area */
			struct wlr_box visible;
			if (!box_intersect(&visible, &c->geom, &m->w)) {
				wlr_scene_node_set_enabled(&c->scene->node, false);
			} else {
				wlr_scene_node_set_enabled(&c->scene->node, true);

				if (visible.x == c->geom.x && visible.width == c->geom.width) {
					struct wlr_box clip;
					swl_client_get_clip(c, &clip);
					wlr_scene_subsurface_tree_set_clip(&c->scene_surface->node, &clip);
				} else {
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

				if (c->shadow)
					wlr_scene_node_set_enabled(&c->shadow->node, true);
			}
		}
	}

	free(all);
	free(col_start);
	free(col_count);
	free(vx);
	free(vw);
}

void
swl_cmd_scroller_cycle_width(SwlServer *server, const Arg *arg)
{
	Client *c = swl_focustop(server, server->selmon);
	if (!c || c->isfloating || c->isfullscreen)
		return;

	c = find_column_head(server, c, server->selmon);

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

	c = find_column_head(server, c, server->selmon);

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
