#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wlr/backend/session.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_output.h>
#include <scenefx/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_foreign_toplevel_management_v1.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_xdg_shell.h>

#include "commands.h"
#include "client.h"
#include "cursor.h"
#include "layout.h"
#include "output.h"
#include "macros.h"
#include "util.h"

/* File-scope static for signal handler */
static SwlServer *sig_server;

/* Static helper forward declarations */
static void createdecoration(struct wl_listener *listener, void *data);
static void requestdecorationmode(struct wl_listener *listener, void *data);
static void destroydecoration(struct wl_listener *listener, void *data);
static void createnotify(struct wl_listener *listener, void *data);
static void createpopup(struct wl_listener *listener, void *data);
static void commitnotify(struct wl_listener *listener, void *data);
static void commitpopup(struct wl_listener *listener, void *data);
static void maximizenotify(struct wl_listener *listener, void *data);
static void urgent(struct wl_listener *listener, void *data);

void
swl_commands_set_server(SwlServer *s)
{
	sig_server = s;
}

void
swl_handlesig(int signo)
{
	if (signo == SIGCHLD)
		while (waitpid(-1, nullptr, WNOHANG) > 0);
	else if (signo == SIGINT || signo == SIGTERM)
		swl_cmd_quit(sig_server, nullptr);
}

/*
 * Listener setup — called from server.c to wire up server-level listeners
 * handled by this module.
 */
void
swl_commands_setup_listeners(SwlServer *server)
{
	LISTEN(&server->xdg_shell->events.new_toplevel,
			&server->new_xdg_toplevel, createnotify);
	LISTEN(&server->xdg_shell->events.new_popup,
			&server->new_xdg_popup, createpopup);
	LISTEN(&server->xdg_decoration_mgr->events.new_toplevel_decoration,
			&server->new_xdg_decoration, createdecoration);
	LISTEN(&server->activation->events.request_activate,
			&server->request_activate, urgent);
}

/* ===== Public command functions ===== */

void
swl_cmd_spawn(SwlServer *server, const Arg *arg)
{
	(void)server;
	if (fork() == 0) {
		dup2(STDERR_FILENO, STDOUT_FILENO);
		setsid();
		execvp(((char **)arg->v)[0], (char **)arg->v);
		die("swl: execvp %s failed:", ((char **)arg->v)[0]);
	}
}

void
swl_cmd_killclient(SwlServer *server, const Arg *arg)
{
	Client *sel = swl_focustop(server, server->selmon);
	if (sel)
		swl_client_send_close(sel);
}

void
swl_cmd_chvt(SwlServer *server, const Arg *arg)
{
	wlr_session_change_vt(server->session, arg->ui);
}

void
swl_cmd_quit(SwlServer *server, const Arg *arg)
{
	wl_display_terminate(server->dpy);
}

void
swl_cmd_focusstack(SwlServer *server, const Arg *arg)
{
	Client *c, *sel = swl_focustop(server, server->selmon);
	if (!sel || (sel->isfullscreen && !swl_client_has_children(sel)))
		return;

	if (arg->i > 0) {
		wl_list_for_each(c, &sel->link, link) {
			if (&c->link == &server->clients)
				continue;
			if (VISIBLEON(c, server->selmon))
				break;
		}
	} else {
		wl_list_for_each_reverse(c, &sel->link, link) {
			if (&c->link == &server->clients)
				continue;
			if (VISIBLEON(c, server->selmon))
				break;
		}
	}
	swl_focusclient(server, c, 1);
	swl_arrange(server, server->selmon);
}

void
swl_cmd_focusmon(SwlServer *server, const Arg *arg)
{
	int i = 0, nmons = wl_list_length(&server->mons);
	if (nmons) {
		do
			server->selmon = swl_dirtomon(server, arg->i);
		while (!server->selmon->wlr_output->enabled && i++ < nmons);
	}
	swl_focusclient(server, swl_focustop(server, server->selmon), 1);
}

void
swl_cmd_togglefloating(SwlServer *server, const Arg *arg)
{
	Client *sel = swl_focustop(server, server->selmon);
	if (sel && !sel->isfullscreen)
		swl_setfloating(server, sel, !sel->isfloating);
}

void
swl_cmd_togglefullscreen(SwlServer *server, const Arg *arg)
{
	Client *sel = swl_focustop(server, server->selmon);
	if (sel)
		swl_setfullscreen(server, sel, !sel->isfullscreen);
}

void
swl_cmd_tagmon(SwlServer *server, const Arg *arg)
{
	Client *sel = swl_focustop(server, server->selmon);
	if (sel)
		swl_setmon(server, sel, swl_dirtomon(server, arg->i));
}

void
swl_cmd_moveresize(SwlServer *server, const Arg *arg)
{
	if (server->cursor_mode != CurNormal && server->cursor_mode != CurPressed)
		return;
	swl_xytonode(server, server->cursor->x, server->cursor->y,
			nullptr, &server->grabc, nullptr, nullptr, nullptr);
	if (!server->grabc || swl_client_is_unmanaged(server->grabc)
			|| server->grabc->isfullscreen)
		return;

	swl_setfloating(server, server->grabc, true);
	switch (server->cursor_mode = arg->ui) {
	case CurMove:
		server->grabcx = (int)round(server->cursor->x) - server->grabc->geom.x;
		server->grabcy = (int)round(server->cursor->y) - server->grabc->geom.y;
		wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "all-scroll");
		break;
	case CurResize:
		wlr_cursor_warp_closest(server->cursor, nullptr,
				server->grabc->geom.x + server->grabc->geom.width,
				server->grabc->geom.y + server->grabc->geom.height);
		wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "se-resize");
		break;
	}
}

/* ===== Public non-command helpers ===== */

void
swl_focusclient(SwlServer *server, Client *c, int lift)
{
	struct wlr_surface *old = server->seat->keyboard_state.focused_surface;
	int unused_lx, unused_ly, old_client_type;
	Client *old_c = nullptr;
	LayerSurface *old_l = nullptr;

	if (server->locked)
		return;

	if (c && lift)
		wlr_scene_node_raise_to_top(&c->scene->node);

	if (c && swl_client_surface(c) == old)
		return;

	if ((old_client_type = swl_toplevel_from_wlr_surface(old, &old_c, &old_l)) == XDGShell) {
		struct wlr_xdg_popup *popup, *tmp;
		wl_list_for_each_safe(popup, tmp, &old_c->surface.xdg->popups, link)
			wlr_xdg_popup_destroy(popup);
	}

	if (c && !swl_client_is_unmanaged(c)) {
		wl_list_remove(&c->flink);
		wl_list_insert(&server->fstack, &c->flink);
		server->selmon = c->mon;
		c->isurgent = false;
		if (!server->exclusive_focus && !server->seat->drag)
			swl_client_set_border_color(c, server->config.focuscolor);
	}

	if (old && (!c || swl_client_surface(c) != old)) {
		if (old_client_type == LayerShell
				&& wlr_scene_node_coords(&old_l->scene->node, &unused_lx, &unused_ly)
				&& old_l->layer_surface->current.layer >= ZWLR_LAYER_SHELL_V1_LAYER_TOP) {
			return;
		} else if (old_c && old_c == server->exclusive_focus
				&& swl_client_wants_focus(old_c)) {
			return;
		} else if (old_c && !swl_client_is_unmanaged(old_c)
				&& (!c || !swl_client_wants_focus(c))) {
			swl_client_set_border_color(old_c, server->config.bordercolor);
			swl_client_activate_surface(old, 0);
			if (old_c->foreign_toplevel)
				wlr_foreign_toplevel_handle_v1_set_activated(old_c->foreign_toplevel, 0);
		}
	}

	swl_printstatus(server);

	if (!c) {
		wlr_seat_keyboard_notify_clear_focus(server->seat);
		return;
	}

	swl_motionnotify(server, 0, nullptr, 0, 0, 0, 0);
	swl_client_notify_enter(server->seat, swl_client_surface(c),
			wlr_seat_get_keyboard(server->seat));
	swl_client_activate_surface(swl_client_surface(c), 1);
	if (c->foreign_toplevel)
		wlr_foreign_toplevel_handle_v1_set_activated(c->foreign_toplevel, 1);
}

Client *
swl_focustop(SwlServer *server, Monitor *m)
{
	Client *c;
	wl_list_for_each(c, &server->fstack, flink) {
		if (VISIBLEON(c, m))
			return c;
	}
	return nullptr;
}

void
swl_setmon(SwlServer *server, Client *c, Monitor *m)
{
	Monitor *oldmon = c->mon;

	if (oldmon == m)
		return;
	c->mon = m;
	c->scroller_continuation = false;
	c->prev = c->geom;

	if (c->foreign_toplevel) {
		if (oldmon)
			wlr_foreign_toplevel_handle_v1_output_leave(c->foreign_toplevel, oldmon->wlr_output);
		if (m)
			wlr_foreign_toplevel_handle_v1_output_enter(c->foreign_toplevel, m->wlr_output);
	}

	if (oldmon)
		swl_arrange(server, oldmon);
	if (m) {
		swl_resize(server, c, c->geom, 0);
		swl_setfullscreen(server, c, c->isfullscreen);
		swl_setfloating(server, c, c->isfloating);
	}
	swl_focusclient(server, swl_focustop(server, server->selmon), 1);
}

void
swl_setfloating(SwlServer *server, Client *c, int floating)
{
	Client *p = swl_client_get_parent(c);
	c->isfloating = floating;
	if (!floating)
		c->scroller_continuation = false;
	if (!c->mon || !swl_client_surface(c)->mapped)
		return;
	wlr_scene_node_reparent(&c->scene->node,
			server->layers[c->isfullscreen || (p && p->isfullscreen)
			? LyrFS : c->isfloating ? LyrFloat : LyrTile]);
	if (floating && !c->isfullscreen) {
		c->geom.x = c->mon->w.x + (c->mon->w.width - c->geom.width) / 2;
		c->geom.y = c->mon->w.y + (c->mon->w.height - c->geom.height) / 2;
		swl_resize(server, c, c->geom, 0);
	}
	swl_arrange(server, c->mon);
	swl_printstatus(server);
}

void
swl_setfullscreen(SwlServer *server, Client *c, int fullscreen)
{
	c->isfullscreen = fullscreen;
	if (c->foreign_toplevel)
		wlr_foreign_toplevel_handle_v1_set_fullscreen(c->foreign_toplevel, fullscreen);
	if (!c->mon || !swl_client_surface(c)->mapped)
		return;
	c->bw = fullscreen ? 0 : server->config.borderpx;
	swl_client_set_fullscreen(c, fullscreen);
	wlr_scene_node_reparent(&c->scene->node,
			server->layers[c->isfullscreen ? LyrFS : c->isfloating ? LyrFloat : LyrTile]);

	if (c->border)
		wlr_scene_node_set_enabled(&c->border->node, !fullscreen);
	if (c->shadow)
		wlr_scene_node_set_enabled(&c->shadow->node, !fullscreen);

	if (fullscreen) {
		c->prev = c->geom;
		swl_resize(server, c, c->mon->m, 0);
	} else {
		swl_resize(server, c, c->prev, 0);
	}
	swl_arrange(server, c->mon);
	swl_printstatus(server);
}

void
swl_applyrules(SwlServer *server, Client *c)
{
	const char *appid, *title;
	int i;
	const Rule *r;
	Monitor *mon = server->selmon, *m;

	appid = swl_client_get_appid(c);
	title = swl_client_get_title(c);

	for (size_t ri = 0; ri < server->config.rules_count; ri++) {
		r = &server->config.rules[ri];
		if ((!r->title || strstr(title, r->title))
				&& (!r->id || strstr(appid, r->id))) {
			c->isfloating = r->isfloating;
			i = 0;
			wl_list_for_each(m, &server->mons, link) {
				if (r->monitor == i++)
					mon = m;
			}
		}
	}
	c->isfloating |= swl_client_is_float_type(c);
	swl_setmon(server, c, mon);
}

/* ===== Static listener helpers ===== */

static void
createdecoration(struct wl_listener *listener, void *data)
{
	struct wlr_xdg_toplevel_decoration_v1 *deco = data;
	Client *c = deco->toplevel->base->data;
	c->decoration = deco;

	LISTEN(&deco->events.request_mode, &c->set_decoration_mode, requestdecorationmode);
	LISTEN(&deco->events.destroy, &c->destroy_decoration, destroydecoration);
	requestdecorationmode(&c->set_decoration_mode, deco);
}

static void
requestdecorationmode(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, set_decoration_mode);
	if (c->surface.xdg->initialized)
		wlr_xdg_toplevel_decoration_v1_set_mode(c->decoration,
				WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
}

static void
destroydecoration(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, destroy_decoration);
	wl_list_remove(&c->destroy_decoration.link);
	wl_list_remove(&c->set_decoration_mode.link);
}

static void
createnotify(struct wl_listener *listener, void *data)
{
	SwlServer *server = wl_container_of(listener, server, new_xdg_toplevel);
	struct wlr_xdg_toplevel *toplevel = data;
	Client *c = nullptr;

	c = toplevel->base->data = ecalloc(1, sizeof(*c));
	c->surface.xdg = toplevel->base;
	c->bw = server->config.borderpx;
	c->server = server;

	LISTEN(&toplevel->base->surface->events.commit, &c->commit, commitnotify);
	LISTEN(&toplevel->base->surface->events.map, &c->map, swl_handle_map);
	LISTEN(&toplevel->base->surface->events.unmap, &c->unmap, swl_handle_unmap);
	LISTEN(&toplevel->events.destroy, &c->destroy, swl_handle_destroy);
	LISTEN(&toplevel->events.request_fullscreen, &c->fullscreen, swl_handle_fullscreen);
	LISTEN(&toplevel->events.request_maximize, &c->maximize, maximizenotify);
	LISTEN(&toplevel->events.set_title, &c->set_title, swl_handle_update_title);
}

static void
createpopup(struct wl_listener *listener, void *data)
{
	struct wlr_xdg_popup *popup = data;
	struct wl_listener *commit_listener = ecalloc(1, sizeof(*commit_listener));
	commit_listener->notify = commitpopup;
	wl_signal_add(&popup->base->surface->events.commit, commit_listener);
}

static void
apply_buffer_effects(struct wlr_scene_buffer *buf, int sx, int sy, void *data)
{
	Client *c = data;
	SwlServer *server = c->server;
	int inner_radius = server->config.corner_radius > (int)c->bw
		? server->config.corner_radius - (int)c->bw : 0;

	if (inner_radius > 0)
		wlr_scene_buffer_set_corner_radius(buf, inner_radius);
	if (server->config.opacity < 1.0f)
		wlr_scene_buffer_set_opacity(buf, server->config.opacity);
}

static void
commitnotify(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, commit);
	SwlServer *server = c->server;

	if (c->surface.xdg->initial_commit) {
		swl_applyrules(server, c);
		if (c->mon)
			swl_client_set_scale(swl_client_surface(c), c->mon->wlr_output->scale);
		swl_setmon(server, c, nullptr);
		wlr_xdg_toplevel_set_wm_capabilities(c->surface.xdg->toplevel,
				WLR_XDG_TOPLEVEL_WM_CAPABILITIES_FULLSCREEN);
		if (c->decoration)
			requestdecorationmode(&c->set_decoration_mode, c->decoration);
		wlr_xdg_toplevel_set_size(c->surface.xdg->toplevel, 0, 0);
		return;
	}

	swl_resize(server, c, c->geom, (c->isfloating && !c->isfullscreen));
	if (c->resize && c->resize <= c->surface.xdg->current.configure_serial)
		c->resize = 0;

	if (server->config.corner_radius > 0 || server->config.opacity < 1.0f)
		wlr_scene_node_for_each_buffer(&c->scene_surface->node,
				apply_buffer_effects, c);
}

static void
commitpopup(struct wl_listener *listener, void *data)
{
	struct wlr_surface *surface = data;
	struct wlr_xdg_popup *popup = wlr_xdg_popup_try_from_wlr_surface(surface);
	LayerSurface *l = nullptr;
	Client *c = nullptr;
	struct wlr_box box;
	int type = -1;

	if (!popup->base->initial_commit)
		return;

	type = swl_toplevel_from_wlr_surface(popup->base->surface, &c, &l);
	if (!popup->parent || type < 0)
		return;

	popup->base->surface->data = wlr_scene_xdg_surface_create(
			popup->parent->data, popup->base);
	if ((l && !l->mon) || (c && !c->mon)) {
		wlr_xdg_popup_destroy(popup);
		return;
	}

	box = type == LayerShell ? l->mon->m : c->mon->w;
	box.x -= (type == LayerShell ? l->scene->node.x : c->geom.x);
	box.y -= (type == LayerShell ? l->scene->node.y : c->geom.y);
	wlr_xdg_popup_unconstrain_from_box(popup, &box);

	wl_list_remove(&listener->link);
	free(listener);
}

static void
factivatenotify(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, factivate);
	struct wlr_foreign_toplevel_handle_v1_activated_event *event = data;
	SwlServer *server = c->server;
	(void)event;
	swl_focusclient(server, c, 1);
	swl_arrange(server, c->mon);
}

static void
fclosenotify(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, fclose);
	swl_client_send_close(c);
}

static void
ffullscreennotify(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, ffullscreen);
	struct wlr_foreign_toplevel_handle_v1_fullscreen_event *event = data;
	swl_setfullscreen(c->server, c, event->fullscreen);
}

static void
fdestroynotify(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, fdestroy);
	wl_list_remove(&c->factivate.link);
	wl_list_remove(&c->fclose.link);
	wl_list_remove(&c->ffullscreen.link);
	wl_list_remove(&c->fdestroy.link);
	c->foreign_toplevel = nullptr;
}

static void
createforeigntoplevel(Client *c)
{
	SwlServer *server = c->server;
	struct wlr_foreign_toplevel_handle_v1 *handle;

	handle = wlr_foreign_toplevel_handle_v1_create(server->foreign_toplevel_mgr);
	if (!handle)
		return;
	c->foreign_toplevel = handle;

	const char *appid = swl_client_get_appid(c);
	const char *title = swl_client_get_title(c);
	if (appid)
		wlr_foreign_toplevel_handle_v1_set_app_id(handle, appid);
	if (title)
		wlr_foreign_toplevel_handle_v1_set_title(handle, title);
	if (c->mon)
		wlr_foreign_toplevel_handle_v1_output_enter(handle, c->mon->wlr_output);

	LISTEN(&handle->events.request_activate, &c->factivate, factivatenotify);
	LISTEN(&handle->events.request_close, &c->fclose, fclosenotify);
	LISTEN(&handle->events.request_fullscreen, &c->ffullscreen, ffullscreennotify);
	LISTEN(&handle->events.destroy, &c->fdestroy, fdestroynotify);
}

void
swl_handle_map(struct wl_listener *listener, void *data)
{
	Client *p = nullptr;
	Client *w, *c = wl_container_of(listener, c, map);
	SwlServer *server = c->server;
	Monitor *m;

	c->scene = swl_client_surface(c)->data = wlr_scene_tree_create(server->layers[LyrTile]);
	wlr_scene_node_set_enabled(&c->scene->node, swl_client_is_unmanaged(c));
	c->scene_surface = c->type == XDGShell
			? wlr_scene_xdg_surface_create(c->scene, c->surface.xdg)
			: wlr_scene_subsurface_tree_create(c->scene, swl_client_surface(c));
	c->scene->node.data = c->scene_surface->node.data = c;

	swl_client_get_geometry(c, &c->geom);
	if (swl_client_is_unmanaged(c)) {
		wlr_scene_node_reparent(&c->scene->node, server->layers[LyrFloat]);
		wlr_scene_node_set_position(&c->scene->node, c->geom.x, c->geom.y);
		swl_client_set_size(c, c->geom.width, c->geom.height);
		if (swl_client_wants_focus(c)) {
			swl_focusclient(server, c, 1);
			server->exclusive_focus = c;
		}
		goto unset_fullscreen;
	}

	{
		const float *bcolor = c->isurgent
			? server->config.urgentcolor : server->config.bordercolor;
		c->border = wlr_scene_rect_create(c->scene,
				c->geom.width, c->geom.height, bcolor);
		c->border->node.data = c;
		wlr_scene_node_place_below(&c->border->node,
				&c->scene_surface->node);

		if (server->config.corner_radius > 0)
			wlr_scene_rect_set_corner_radius(c->border,
					server->config.corner_radius);

		if (server->config.shadow_enabled) {
			c->shadow = wlr_scene_shadow_create(c->scene,
					c->geom.width, c->geom.height,
					server->config.corner_radius,
					server->config.shadow_sigma,
					server->config.shadow_color);
			wlr_scene_node_place_below(&c->shadow->node,
					&c->border->node);
		}
	}

	swl_client_set_tiled(c, WLR_EDGE_TOP | WLR_EDGE_BOTTOM | WLR_EDGE_LEFT | WLR_EDGE_RIGHT);
	c->geom.width += 2 * c->bw;
	c->geom.height += 2 * c->bw;

	{
		Monitor *cm = c->mon ? c->mon : server->selmon;
		Client *focused = swl_focustop(server, cm);
		if (focused && !c->isfloating) {
			/* Insert after the last member of the focused client's
			 * column so we don't split a stacked column */
			Client *last = focused;
			Client *iter;
			wl_list_for_each(iter, &focused->link, link) {
				if (&iter->link == &server->clients)
					break;
				if (!VISIBLEON(iter, cm) || iter->isfloating || iter->isfullscreen)
					continue;
				if (!iter->scroller_continuation)
					break;
				last = iter;
			}
			wl_list_insert(&last->link, &c->link);
		} else {
			wl_list_insert(&server->clients, &c->link);
		}
	}
	wl_list_insert(&server->fstack, &c->flink);

	if ((p = swl_client_get_parent(c))) {
		c->isfloating = true;
		swl_setmon(server, c, p->mon);
	} else {
		swl_applyrules(server, c);
	}

	if (!swl_client_is_unmanaged(c))
		createforeigntoplevel(c);

	swl_printstatus(server);

unset_fullscreen:
	m = c->mon ? c->mon : swl_xytomon(server, c->geom.x, c->geom.y);
	wl_list_for_each(w, &server->clients, link) {
		if (w != c && w != p && w->isfullscreen && m == w->mon)
			swl_setfullscreen(server, w, false);
	}
}

static void
maximizenotify(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, maximize);
	if (c->surface.xdg->initialized
			&& wl_resource_get_version(c->surface.xdg->toplevel->resource)
			< XDG_TOPLEVEL_WM_CAPABILITIES_SINCE_VERSION)
		wlr_xdg_surface_schedule_configure(c->surface.xdg);
}

void
swl_handle_unmap(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, unmap);
	SwlServer *server = c->server;

	if (c == server->grabc) {
		server->cursor_mode = CurNormal;
		server->grabc = nullptr;
	}

	if (swl_client_is_unmanaged(c)) {
		if (c == server->exclusive_focus) {
			server->exclusive_focus = nullptr;
			swl_focusclient(server, swl_focustop(server, server->selmon), 1);
		}
	} else {
		/* If c is a column head, promote the next continuation */
		if (!c->scroller_continuation) {
			Client *next;
			wl_list_for_each(next, &c->link, link) {
				if (&next->link == &server->clients)
					break;
				if (next->mon != c->mon || next->isfloating || next->isfullscreen)
					continue;
				if (next->scroller_continuation) {
					next->scroller_continuation = false;
					next->scroller_cw = c->scroller_cw;
					next->scroller_preset_idx = c->scroller_preset_idx;
				}
				break;
			}
		}
		wl_list_remove(&c->link);
		swl_setmon(server, c, nullptr);
		wl_list_remove(&c->flink);
	}

	if (c->foreign_toplevel)
		wlr_foreign_toplevel_handle_v1_destroy(c->foreign_toplevel);

	wlr_scene_node_destroy(&c->scene->node);
	swl_printstatus(server);
	swl_motionnotify(server, 0, nullptr, 0, 0, 0, 0);
}

void
swl_handle_destroy(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, destroy);
	wl_list_remove(&c->destroy.link);
	wl_list_remove(&c->set_title.link);
	wl_list_remove(&c->fullscreen.link);
#ifdef XWAYLAND
	if (c->type != XDGShell) {
		wl_list_remove(&c->activate.link);
		wl_list_remove(&c->associate.link);
		wl_list_remove(&c->configure.link);
		wl_list_remove(&c->dissociate.link);
		wl_list_remove(&c->set_hints.link);
	} else
#endif
	{
		wl_list_remove(&c->commit.link);
		wl_list_remove(&c->map.link);
		wl_list_remove(&c->unmap.link);
		wl_list_remove(&c->maximize.link);
	}
	free(c);
}

void
swl_handle_fullscreen(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, fullscreen);
	swl_setfullscreen(c->server, c, swl_client_wants_fullscreen(c));
}

void
swl_handle_update_title(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, set_title);
	if (c->foreign_toplevel) {
		const char *title = swl_client_get_title(c);
		if (title)
			wlr_foreign_toplevel_handle_v1_set_title(c->foreign_toplevel, title);
	}
	if (c == swl_focustop(c->server, c->mon))
		swl_printstatus(c->server);
}

static void
urgent(struct wl_listener *listener, void *data)
{
	SwlServer *server = wl_container_of(listener, server, request_activate);
	struct wlr_xdg_activation_v1_request_activate_event *event = data;
	Client *c = nullptr;

	swl_toplevel_from_wlr_surface(event->surface, &c, nullptr);
	if (!c || c == swl_focustop(server, server->selmon))
		return;

	c->isurgent = true;
	swl_printstatus(server);
	if (swl_client_surface(c)->mapped)
		swl_client_set_border_color(c, server->config.urgentcolor);
}
