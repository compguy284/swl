#include <math.h>
#include <stdlib.h>
#include <time.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_cursor_shape_v1.h>
#include <wlr/types/wlr_idle_notify_v1.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_pointer_constraints_v1.h>
#include <wlr/types/wlr_relative_pointer_v1.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/util/region.h>

#include "cursor.h"
#include "client.h"
#include "commands.h"
#include "layout.h"
#include "macros.h"
#include "util.h"

static void cursorconstrain(SwlServer *server, struct wlr_pointer_constraint_v1 *constraint);
static void cursorwarptohint(SwlServer *server);
static void destroypointerconstraint(struct wl_listener *listener, void *data);

void
swl_handle_cursor_axis(struct wl_listener *listener, void *data)
{
	SwlServer *server = wl_container_of(listener, server, cursor_axis);
	struct wlr_pointer_axis_event *event = data;

	wlr_idle_notifier_v1_notify_activity(server->idle_notifier, server->seat);
	wlr_seat_pointer_notify_axis(server->seat, event->time_msec,
			event->orientation, event->delta, event->delta_discrete,
			event->source, event->relative_direction);
}

void
swl_handle_cursor_button(struct wl_listener *listener, void *data)
{
	SwlServer *server = wl_container_of(listener, server, cursor_button);
	struct wlr_pointer_button_event *event = data;
	struct wlr_keyboard *keyboard;
	uint32_t mods;
	Client *c;
	const Button *b;
	size_t i;

	wlr_idle_notifier_v1_notify_activity(server->idle_notifier, server->seat);

	switch (event->state) {
	case WL_POINTER_BUTTON_STATE_PRESSED:
		server->cursor_mode = CurPressed;
		server->selmon = swl_xytomon(server, server->cursor->x, server->cursor->y);
		if (server->locked)
			break;
		swl_xytonode(server, server->cursor->x, server->cursor->y, nullptr, &c, nullptr, nullptr, nullptr);
		if (c && (!swl_client_is_unmanaged(c) || swl_client_wants_focus(c)))
			swl_focusclient(server, c, 1);
		keyboard = wlr_seat_get_keyboard(server->seat);
		mods = keyboard ? wlr_keyboard_get_modifiers(keyboard) : 0;
		for (i = 0; i < server->config.buttons_count; i++) {
			b = &server->config.buttons[i];
			if (CLEANMASK(mods) == CLEANMASK(b->mod)
					&& event->button == b->button && b->func) {
				b->func(server, &b->arg);
				return;
			}
		}
		break;
	case WL_POINTER_BUTTON_STATE_RELEASED:
		if (!server->locked && server->cursor_mode != CurNormal
				&& server->cursor_mode != CurPressed) {
			wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "default");
			server->cursor_mode = CurNormal;
			server->selmon = swl_xytomon(server, server->cursor->x, server->cursor->y);
			swl_setmon(server, server->grabc, server->selmon, 0);
			server->grabc = nullptr;
			return;
		}
		server->cursor_mode = CurNormal;
		break;
	}

	wlr_seat_pointer_notify_button(server->seat, event->time_msec,
			event->button, event->state);
}

void
swl_handle_cursor_frame(struct wl_listener *listener, void *data)
{
	SwlServer *server = wl_container_of(listener, server, cursor_frame);
	wlr_seat_pointer_notify_frame(server->seat);
}

void
swl_handle_cursor_motion(struct wl_listener *listener, void *data)
{
	SwlServer *server = wl_container_of(listener, server, cursor_motion);
	struct wlr_pointer_motion_event *event = data;

	swl_motionnotify(server, event->time_msec, &event->pointer->base,
			event->delta_x, event->delta_y,
			event->unaccel_dx, event->unaccel_dy);
}

void
swl_handle_cursor_motion_absolute(struct wl_listener *listener, void *data)
{
	SwlServer *server = wl_container_of(listener, server, cursor_motion_absolute);
	struct wlr_pointer_motion_absolute_event *event = data;
	double lx, ly, dx, dy;

	if (!event->time_msec)
		wlr_cursor_warp_absolute(server->cursor, &event->pointer->base,
				event->x, event->y);
	wlr_cursor_absolute_to_layout_coords(server->cursor, &event->pointer->base,
			event->x, event->y, &lx, &ly);
	dx = lx - server->cursor->x;
	dy = ly - server->cursor->y;
	swl_motionnotify(server, event->time_msec, &event->pointer->base,
			dx, dy, dx, dy);
}

void
swl_handle_set_cursor(struct wl_listener *listener, void *data)
{
	SwlServer *server = wl_container_of(listener, server, request_cursor);
	struct wlr_seat_pointer_request_set_cursor_event *event = data;

	if (server->cursor_mode != CurNormal && server->cursor_mode != CurPressed)
		return;
	if (event->seat_client == server->seat->pointer_state.focused_client)
		wlr_cursor_set_surface(server->cursor, event->surface,
				event->hotspot_x, event->hotspot_y);
}

void
swl_handle_set_cursor_shape(struct wl_listener *listener, void *data)
{
	SwlServer *server = wl_container_of(listener, server, request_set_cursor_shape);
	struct wlr_cursor_shape_manager_v1_request_set_shape_event *event = data;

	if (server->cursor_mode != CurNormal && server->cursor_mode != CurPressed)
		return;
	if (event->seat_client == server->seat->pointer_state.focused_client)
		wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr,
				wlr_cursor_shape_v1_name(event->shape));
}

void
swl_handle_new_pointer_constraint(struct wl_listener *listener, void *data)
{
	SwlServer *server = wl_container_of(listener, server, new_pointer_constraint);
	PointerConstraint *pointer_constraint = ecalloc(1, sizeof(*pointer_constraint));

	pointer_constraint->constraint = data;
	pointer_constraint->server = server;
	LISTEN(&pointer_constraint->constraint->events.destroy,
			&pointer_constraint->destroy, destroypointerconstraint);
}

void
swl_motionnotify(SwlServer *server, uint32_t time, struct wlr_input_device *device,
		double dx, double dy, double dx_unaccel, double dy_unaccel)
{
	double sx = 0, sy = 0, sx_confined, sy_confined;
	Client *c = nullptr, *w = nullptr;
	LayerSurface *l = nullptr;
	struct wlr_surface *surface = nullptr;
	struct wlr_pointer_constraint_v1 *constraint;

	swl_xytonode(server, server->cursor->x, server->cursor->y,
			&surface, &c, nullptr, &sx, &sy);

	if (server->cursor_mode == CurPressed && !server->seat->drag
			&& surface != server->seat->pointer_state.focused_surface
			&& swl_toplevel_from_wlr_surface(
				server->seat->pointer_state.focused_surface, &w, &l) >= 0) {
		c = w;
		surface = server->seat->pointer_state.focused_surface;
		sx = server->cursor->x - (l ? l->scene->node.x : w->geom.x);
		sy = server->cursor->y - (l ? l->scene->node.y : w->geom.y);
	}

	if (time) {
		wlr_relative_pointer_manager_v1_send_relative_motion(
				server->relative_pointer_mgr, server->seat,
				(uint64_t)time * 1000, dx, dy, dx_unaccel, dy_unaccel);

		wl_list_for_each(constraint,
				&server->pointer_constraints->constraints, link)
			cursorconstrain(server, constraint);

		if (server->active_constraint
				&& server->cursor_mode != CurResize
				&& server->cursor_mode != CurMove) {
			swl_toplevel_from_wlr_surface(server->active_constraint->surface,
					&c, nullptr);
			if (c && server->active_constraint->surface
					== server->seat->pointer_state.focused_surface) {
				sx = server->cursor->x - c->geom.x - c->bw;
				sy = server->cursor->y - c->geom.y - c->bw;
				if (wlr_region_confine(&server->active_constraint->region,
						sx, sy, sx + dx, sy + dy,
						&sx_confined, &sy_confined)) {
					dx = sx_confined - sx;
					dy = sy_confined - sy;
				}
				if (server->active_constraint->type
						== WLR_POINTER_CONSTRAINT_V1_LOCKED)
					return;
			}
		}

		wlr_cursor_move(server->cursor, device, dx, dy);
		wlr_idle_notifier_v1_notify_activity(server->idle_notifier, server->seat);
		if (server->config.sloppyfocus)
			server->selmon = swl_xytomon(server, server->cursor->x,
					server->cursor->y);
	}

	wlr_scene_node_set_position(&server->drag_icon->node,
			(int)round(server->cursor->x), (int)round(server->cursor->y));

	if (server->cursor_mode == CurMove) {
		swl_resize(server, server->grabc, (struct wlr_box){
				.x = (int)round(server->cursor->x) - server->grabcx,
				.y = (int)round(server->cursor->y) - server->grabcy,
				.width = server->grabc->geom.width,
				.height = server->grabc->geom.height}, 1);
		return;
	} else if (server->cursor_mode == CurResize) {
		swl_resize(server, server->grabc, (struct wlr_box){
				.x = server->grabc->geom.x,
				.y = server->grabc->geom.y,
				.width = (int)round(server->cursor->x) - server->grabc->geom.x,
				.height = (int)round(server->cursor->y) - server->grabc->geom.y}, 1);
		return;
	}

	if (!surface && !server->seat->drag)
		wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "default");

	swl_pointerfocus(server, c, surface, sx, sy, time);
}

void
swl_pointerfocus(SwlServer *server, Client *c, struct wlr_surface *surface,
		double sx, double sy, uint32_t time)
{
	struct timespec now;

	if (surface != server->seat->pointer_state.focused_surface
			&& server->config.sloppyfocus && time && c
			&& !swl_client_is_unmanaged(c))
		swl_focusclient(server, c, 0);

	if (!surface) {
		wlr_seat_pointer_notify_clear_focus(server->seat);
		return;
	}

	if (!time) {
		clock_gettime(CLOCK_MONOTONIC, &now);
		time = now.tv_sec * 1000 + now.tv_nsec / 1000000;
	}

	wlr_seat_pointer_notify_enter(server->seat, surface, sx, sy);
	wlr_seat_pointer_notify_motion(server->seat, time, sx, sy);
}

void
swl_xytonode(SwlServer *server, double x, double y,
		struct wlr_surface **psurface, Client **pc, LayerSurface **pl,
		double *nx, double *ny)
{
	struct wlr_scene_node *node, *pnode;
	struct wlr_surface *surface = nullptr;
	Client *c = nullptr;
	LayerSurface *l = nullptr;
	int layer;

	for (layer = NUM_LAYERS - 1; !surface && layer >= 0; layer--) {
		if (!(node = wlr_scene_node_at(&server->layers[layer]->node,
				x, y, nx, ny)))
			continue;
		if (node->type == WLR_SCENE_NODE_BUFFER)
			surface = wlr_scene_surface_try_from_buffer(
					wlr_scene_buffer_from_node(node))->surface;
		for (pnode = node; pnode && !c; pnode = &pnode->parent->node)
			c = pnode->data;
		if (c && c->type == LayerShell) {
			c = nullptr;
			l = pnode->data;
		}
	}

	if (psurface) *psurface = surface;
	if (pc) *pc = c;
	if (pl) *pl = l;
}

Monitor *
swl_xytomon(SwlServer *server, double x, double y)
{
	struct wlr_output *o = wlr_output_layout_output_at(server->output_layout,
			x, y);
	return o ? o->data : nullptr;
}

static void
cursorconstrain(SwlServer *server, struct wlr_pointer_constraint_v1 *constraint)
{
	if (server->active_constraint == constraint)
		return;
	if (server->active_constraint)
		wlr_pointer_constraint_v1_send_deactivated(server->active_constraint);
	server->active_constraint = constraint;
	wlr_pointer_constraint_v1_send_activated(constraint);
}

static void
cursorwarptohint(SwlServer *server)
{
	Client *c = nullptr;
	double sx = server->active_constraint->current.cursor_hint.x;
	double sy = server->active_constraint->current.cursor_hint.y;

	swl_toplevel_from_wlr_surface(server->active_constraint->surface, &c, nullptr);
	if (c && server->active_constraint->current.cursor_hint.enabled) {
		wlr_cursor_warp(server->cursor, nullptr,
				sx + c->geom.x + c->bw, sy + c->geom.y + c->bw);
		wlr_seat_pointer_warp(server->active_constraint->seat, sx, sy);
	}
}

static void
destroypointerconstraint(struct wl_listener *listener, void *data)
{
	PointerConstraint *pointer_constraint =
			wl_container_of(listener, pointer_constraint, destroy);
	SwlServer *server = pointer_constraint->server;

	if (server->active_constraint == pointer_constraint->constraint) {
		cursorwarptohint(server);
		server->active_constraint = nullptr;
	}

	wl_list_remove(&pointer_constraint->destroy.link);
	free(pointer_constraint);
}
