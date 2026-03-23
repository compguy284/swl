/*
 * Output/monitor management — extracted from swl.c.
 *
 * Handles monitor creation, destruction, rendering, output management,
 * power management, GPU reset, status printing, and related helpers.
 */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_output_management_v1.h>
#include <wlr/types/wlr_output_power_management_v1.h>
#include <scenefx/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_session_lock_v1.h>

#include "output.h"
#include "animation.h"
#include "client.h"
#include "commands.h"
#include "cursor.h"
#include "ipc.h"
#include "layer.h"
#include "layout.h"
#include "macros.h"
#include "util.h"

/* static forward declarations */
static void rendermon(struct wl_listener *listener, void *data);
static void set_buffer_opacity(struct wlr_scene_buffer *buf, int sx, int sy, void *data);

struct opacity_data {
	float opacity;
};
static void cleanupmon(struct wl_listener *listener, void *data);
static void requestmonstate(struct wl_listener *listener, void *data);
static void closemon(SwlServer *server, Monitor *m);
static void outputmgrapplyortest(SwlServer *server,
		struct wlr_output_configuration_v1 *config, int test);
void
swl_handle_new_output(struct wl_listener *listener, void *data)
{
	/* This event is raised by the backend when a new output (aka a display or
	 * monitor) becomes available. */
	SwlServer *server = wl_container_of(listener, server, new_output);
	struct wlr_output *wlr_output = data;
	size_t ri;
	struct wlr_output_state state;
	Monitor *m;

	if (!wlr_output_init_render(wlr_output, server->alloc, server->drw))
		return;

	m = wlr_output->data = ecalloc(1, sizeof(*m));
	m->wlr_output = wlr_output;
	m->server = server;

	for (size_t i = 0; i < LENGTH(m->layers); i++)
		wl_list_init(&m->layers[i]);

	wlr_output_state_init(&state);
	/* Initialize monitor state using configured rules */
	for (ri = 0; ri < server->config.monrules_count; ri++) {
		const MonitorRule *r = &server->config.monrules[ri];
		if (!r->name || strstr(wlr_output->name, r->name)) {
			m->m.x = r->x;
			m->m.y = r->y;
			wlr_output_state_set_scale(&state, r->scale);
			wlr_output_state_set_transform(&state, r->rr);
			break;
		}
	}

	/* The mode is a tuple of (width, height, refresh rate), and each
	 * monitor supports only a specific set of modes. We just pick the
	 * monitor's preferred mode; a more sophisticated compositor would let
	 * the user configure it. */
	wlr_output_state_set_mode(&state, wlr_output_preferred_mode(wlr_output));

	/* Set up event listeners */
	LISTEN(&wlr_output->events.frame, &m->frame, rendermon);
	LISTEN(&wlr_output->events.destroy, &m->destroy, cleanupmon);
	LISTEN(&wlr_output->events.request_state, &m->request_state, requestmonstate);

	wlr_output_state_set_enabled(&state, 1);
	wlr_output_commit_state(wlr_output, &state);
	wlr_output_state_finish(&state);

	wl_list_insert(&server->mons, &m->link);
	swl_printstatus(server);
	swl_ipc_notify_monitor(server->ipc, m, 1);

	/* The xdg-protocol specifies:
	 *
	 * If the fullscreened surface is not opaque, the compositor must make
	 * sure that other screen content not part of the same surface tree (made
	 * up of subsurfaces, popups or similarly coupled surfaces) are not
	 * visible below the fullscreened surface.
	 *
	 */
	/* swl_handle_layout_change() will resize and set correct position */
	m->fullscreen_bg = wlr_scene_rect_create(server->layers[LyrFS], 0, 0,
			server->config.fullscreen_bg);
	wlr_scene_node_set_enabled(&m->fullscreen_bg->node, 0);

	/* Adds this to the output layout in the order it was configured.
	 *
	 * The output layout utility automatically adds a wl_output global to the
	 * display, which Wayland clients can see to find out information about the
	 * output (such as DPI, scale factor, manufacturer, etc).
	 */
	m->scene_output = wlr_scene_output_create(server->scene, wlr_output);
	if (m->m.x == -1 && m->m.y == -1)
		wlr_output_layout_add_auto(server->output_layout, wlr_output);
	else
		wlr_output_layout_add(server->output_layout, wlr_output, m->m.x, m->m.y);
}

void
swl_handle_layout_change(struct wl_listener *listener, void *data)
{
	/*
	 * Called whenever the output layout changes: adding or removing a
	 * monitor, changing an output's mode or position, etc. This is where
	 * the change officially happens and we update geometry, window
	 * positions, focus, and the stored configuration in wlroots'
	 * output-manager implementation.
	 */
	SwlServer *server = wl_container_of(listener, server, layout_change);
	struct wlr_output_configuration_v1 *config
			= wlr_output_configuration_v1_create();
	Client *c;
	struct wlr_output_configuration_head_v1 *config_head;
	Monitor *m;

	/* First remove from the layout the disabled monitors */
	wl_list_for_each(m, &server->mons, link) {
		if (m->wlr_output->enabled || m->asleep)
			continue;
		config_head = wlr_output_configuration_head_v1_create(config, m->wlr_output);
		config_head->state.enabled = 0;
		/* Remove this output from the layout to avoid cursor enter inside it */
		wlr_output_layout_remove(server->output_layout, m->wlr_output);
		closemon(server, m);
		m->m = m->w = (struct wlr_box){0};
	}
	/* Insert outputs that need to */
	wl_list_for_each(m, &server->mons, link) {
		if (m->wlr_output->enabled
				&& !wlr_output_layout_get(server->output_layout, m->wlr_output))
			wlr_output_layout_add_auto(server->output_layout, m->wlr_output);
	}

	/* Now that we update the output layout we can get its box */
	wlr_output_layout_get_box(server->output_layout, nullptr, &server->sgeom);

	wlr_scene_node_set_position(&server->root_bg->node, server->sgeom.x, server->sgeom.y);
	wlr_scene_rect_set_size(server->root_bg, server->sgeom.width, server->sgeom.height);

	/* Make sure the clients are hidden when swl is locked */
	wlr_scene_node_set_position(&server->locked_bg->node, server->sgeom.x, server->sgeom.y);
	wlr_scene_rect_set_size(server->locked_bg, server->sgeom.width, server->sgeom.height);

	wl_list_for_each(m, &server->mons, link) {
		if (!m->wlr_output->enabled)
			continue;
		config_head = wlr_output_configuration_head_v1_create(config, m->wlr_output);

		/* Get the effective monitor geometry to use for surfaces */
		wlr_output_layout_get_box(server->output_layout, m->wlr_output, &m->m);
		m->w = m->m;
		wlr_scene_output_set_position(m->scene_output, m->m.x, m->m.y);

		wlr_scene_node_set_position(&m->fullscreen_bg->node, m->m.x, m->m.y);
		wlr_scene_rect_set_size(m->fullscreen_bg, m->m.width, m->m.height);

		if (m->lock_surface) {
			struct wlr_scene_tree *scene_tree = m->lock_surface->surface->data;
			wlr_scene_node_set_position(&scene_tree->node, m->m.x, m->m.y);
			wlr_session_lock_surface_v1_configure(m->lock_surface, m->m.width, m->m.height);
		}

		/* Calculate the effective monitor geometry to use for clients */
		swl_arrangelayers(server, m);
		/* Don't move clients to the left output when plugging monitors */
		swl_arrange(server, m);
		/* make sure fullscreen clients have the right size */
		if ((c = swl_focustop(server, m)) && c->isfullscreen)
			swl_resize(server, c, m->m, 0);

		/* Try to re-set the gamma LUT when updating monitors,
		 * it's only really needed when enabling a disabled output, but meh. */
		m->gamma_lut_changed = true;

		config_head->state.x = m->m.x;
		config_head->state.y = m->m.y;

		if (!server->selmon) {
			server->selmon = m;
		}
	}

	if (server->selmon && server->selmon->wlr_output->enabled) {
		wl_list_for_each(c, &server->clients, link) {
			if (!c->mon && swl_client_surface(c)->mapped)
				swl_setmon(server, c, server->selmon);
		}
		swl_focusclient(server, swl_focustop(server, server->selmon), 1);
		if (server->selmon->lock_surface) {
			swl_client_notify_enter(server->seat, server->selmon->lock_surface->surface,
					wlr_seat_get_keyboard(server->seat));
			swl_client_activate_surface(server->selmon->lock_surface->surface, 1);
		}
	}

	/* FIXME: figure out why the cursor image is at 0,0 after turning all
	 * the monitors on.
	 * Move the cursor image where it used to be. It does not generate a
	 * wl_pointer.motion event for the clients, it's only the image what it's
	 * at the wrong position after all. */
	wlr_cursor_move(server->cursor, nullptr, 0, 0);

	wlr_output_manager_v1_set_configuration(server->output_mgr, config);
}

void
swl_handle_output_mgr_apply(struct wl_listener *listener, void *data)
{
	SwlServer *server = wl_container_of(listener, server, output_mgr_apply);
	struct wlr_output_configuration_v1 *config = data;
	outputmgrapplyortest(server, config, 0);
}

void
swl_handle_output_mgr_test(struct wl_listener *listener, void *data)
{
	SwlServer *server = wl_container_of(listener, server, output_mgr_test);
	struct wlr_output_configuration_v1 *config = data;
	outputmgrapplyortest(server, config, 1);
}

void
swl_output_set_power(SwlServer *server, Monitor *m, bool enabled)
{
	struct wlr_output_state state = {0};
	m->gamma_lut_changed = true; /* Reapply gamma LUT when re-enabling the output */
	wlr_output_state_set_enabled(&state, enabled);
	wlr_output_commit_state(m->wlr_output, &state);
	m->asleep = !enabled;
	swl_handle_layout_change(&server->layout_change, nullptr);
}

void
swl_handle_output_power_set_mode(struct wl_listener *listener, void *data)
{
	SwlServer *server = wl_container_of(listener, server, output_power_mgr_set_mode);
	struct wlr_output_power_v1_set_mode_event *event = data;
	Monitor *m = event->output->data;

	if (!m)
		return;

	swl_output_set_power(server, m, event->mode);
}

void
swl_handle_gpu_reset(struct wl_listener *listener, void *data)
{
	SwlServer *server = wl_container_of(listener, server, gpu_reset);
	struct wlr_renderer *old_drw = server->drw;
	struct wlr_allocator *old_alloc = server->alloc;
	Monitor *m;

	if (!(server->drw = wlr_renderer_autocreate(server->backend)))
		die("couldn't recreate renderer");

	if (!(server->alloc = wlr_allocator_autocreate(server->backend, server->drw)))
		die("couldn't recreate allocator");

	wl_list_remove(&server->gpu_reset.link);
	wl_signal_add(&server->drw->events.lost, &server->gpu_reset);

	wlr_compositor_set_renderer(server->compositor, server->drw);

	wl_list_for_each(m, &server->mons, link) {
		wlr_output_init_render(m->wlr_output, server->alloc, server->drw);
	}

	wlr_allocator_destroy(old_alloc);
	wlr_renderer_destroy(old_drw);
}

void
swl_printstatus(SwlServer *server)
{
	Monitor *m = nullptr;
	Client *c;

	wl_list_for_each(m, &server->mons, link) {
		if ((c = swl_focustop(server, m))) {
			printf("%s title %s\n", m->wlr_output->name, swl_client_get_title(c));
			printf("%s appid %s\n", m->wlr_output->name, swl_client_get_appid(c));
			printf("%s fullscreen %d\n", m->wlr_output->name, c->isfullscreen);
			printf("%s floating %d\n", m->wlr_output->name, c->isfloating);
		} else {
			printf("%s title \n", m->wlr_output->name);
			printf("%s appid \n", m->wlr_output->name);
			printf("%s fullscreen \n", m->wlr_output->name);
			printf("%s floating \n", m->wlr_output->name);
		}

		printf("%s selmon %u\n", m->wlr_output->name, m == server->selmon);
	}
	fflush(stdout);
}

Monitor *
swl_dirtomon(SwlServer *server, enum wlr_direction dir)
{
	struct wlr_output *next;
	if (!wlr_output_layout_get(server->output_layout, server->selmon->wlr_output))
		return server->selmon;
	if ((next = wlr_output_layout_adjacent_output(server->output_layout,
			dir, server->selmon->wlr_output, server->selmon->m.x, server->selmon->m.y)))
		return next->data;
	if ((next = wlr_output_layout_farthest_output(server->output_layout,
			dir ^ (WLR_DIRECTION_LEFT|WLR_DIRECTION_RIGHT),
			server->selmon->wlr_output, server->selmon->m.x, server->selmon->m.y)))
		return next->data;
	return server->selmon;
}

/* static helpers and per-monitor listeners */

static void
set_buffer_opacity(struct wlr_scene_buffer *buf, int sx, int sy, void *data)
{
	(void)sx; (void)sy;
	struct opacity_data *od = data;
	wlr_scene_buffer_set_opacity(buf, od->opacity);
}

static void
rendermon(struct wl_listener *listener, void *data)
{
	/* This function is called every time an output is ready to display a frame,
	 * generally at the output's refresh rate (e.g. 60Hz). */
	Monitor *m = wl_container_of(listener, m, frame);
	SwlServer *server = m->server;
	Client *c;
	struct wlr_output_state pending = {0};
	struct timespec now;
	bool need_more_frames = false;

	/* Tick fadeout (close) animations */
	SwlFadeout *fo, *fo_tmp;
	wl_list_for_each_safe(fo, fo_tmp, &server->fadeout_clients, link) {
		if (!fo->animation.running) {
			wlr_scene_node_destroy(&fo->snapshot->node);
			wl_list_remove(&fo->link);
			free(fo);
			continue;
		}
		double factor = swl_animation_tick(&fo->animation);
		if (factor >= 0) {
			wlr_scene_node_set_position(&fo->snapshot->node,
				fo->animation.current.x, fo->animation.current.y);
			/* Fade opacity from start_opacity down to 0 */
			float opacity = fo->start_opacity * (1.0f - (float)factor);
			struct wlr_scene_node *child;
			wl_list_for_each(child, &fo->snapshot->children, link) {
				if (child->type == WLR_SCENE_NODE_BUFFER)
					wlr_scene_buffer_set_opacity(
						wlr_scene_buffer_from_node(child), opacity);
			}
			need_more_frames = true;
		}
	}

	/* Tick client animations */
	if (server->config.animations) {
		wl_list_for_each(c, &server->clients, link) {
			if (!c->animation.running)
				continue;
			double factor = swl_animation_tick(&c->animation);
			if (factor >= 0) {
				wlr_scene_node_set_position(&c->scene->node,
					c->animation.current.x, c->animation.current.y);

				/* Fade-in opacity for open animations */
				if (c->animation.action == AnimOpen && c->scene_surface) {
					float opacity = server->config.anim_fade_start_opacity
						+ (1.0f - server->config.anim_fade_start_opacity) * (float)factor;
					struct opacity_data od = { .opacity = opacity * server->config.opacity };
					wlr_scene_node_for_each_buffer(&c->scene_surface->node,
						set_buffer_opacity, &od);
				}

				/* Clip tiled clients to monitor bounds during animation */
				if (!c->isfloating && !c->isfullscreen && c->mon)
					swl_clip_animated(server, c);

				need_more_frames = true;
			}
			/* When animation finishes, snap to final position */
			if (!c->animation.running) {
				wlr_scene_node_set_position(&c->scene->node,
					c->geom.x, c->geom.y);
				if (c->scene_surface) {
					struct opacity_data od = { .opacity = server->config.opacity };
					wlr_scene_node_for_each_buffer(&c->scene_surface->node,
						set_buffer_opacity, &od);
				}
				if (!c->isfloating && !c->isfullscreen && c->mon)
					swl_clip_animated(server, c);
			}
		}
	}

	/* Render if no XDG clients have an outstanding resize and are visible on
	 * this monitor. */
	wl_list_for_each(c, &server->clients, link) {
		if (c->resize && !c->isfloating && swl_client_is_rendered_on_mon(c, m) && !swl_client_is_stopped(c))
			goto skip;
	}

	wlr_scene_output_commit(m->scene_output, nullptr);

skip:
	/* Let clients know a frame has been rendered */
	clock_gettime(CLOCK_MONOTONIC, &now);
	wlr_scene_output_send_frame_done(m->scene_output, &now);
	wlr_output_state_finish(&pending);

	if (need_more_frames)
		wlr_output_schedule_frame(m->wlr_output);
}

static void
closemon(SwlServer *server, Monitor *m)
{
	/* update selmon if needed and
	 * move closed monitor's clients to the focused one */
	Client *c;
	int i = 0, nmons = wl_list_length(&server->mons);
	if (!nmons) {
		server->selmon = nullptr;
	} else if (m == server->selmon) {
		do /* don't switch to disabled mons */
			server->selmon = wl_container_of(server->mons.next, server->selmon, link);
		while (!server->selmon->wlr_output->enabled && i++ < nmons);

		if (!server->selmon->wlr_output->enabled)
			server->selmon = nullptr;
	}

	wl_list_for_each(c, &server->clients, link) {
		if (c->isfloating && c->geom.x > m->m.width)
			swl_resize(server, c, (struct wlr_box){.x = c->geom.x - m->w.width, .y = c->geom.y,
					.width = c->geom.width, .height = c->geom.height}, 0);
		if (c->mon == m)
			swl_setmon(server, c, server->selmon);
	}
	swl_focusclient(server, swl_focustop(server, server->selmon), 1);
	swl_printstatus(server);
	swl_ipc_notify_monitor(server->ipc, m, 0);
}

static void
cleanupmon(struct wl_listener *listener, void *data)
{
	Monitor *m = wl_container_of(listener, m, destroy);
	SwlServer *server = m->server;
	LayerSurface *l, *tmp;
	size_t i;

	/* m->layers[i] are intentionally not unlinked */
	for (i = 0; i < LENGTH(m->layers); i++) {
		wl_list_for_each_safe(l, tmp, &m->layers[i], link)
			wlr_layer_surface_v1_destroy(l->layer_surface);
	}

	wl_list_remove(&m->destroy.link);
	wl_list_remove(&m->frame.link);
	wl_list_remove(&m->link);
	wl_list_remove(&m->request_state.link);
	if (m->lock_surface)
		m->destroy_lock_surface.notify(&m->destroy_lock_surface, nullptr);
	m->wlr_output->data = nullptr;
	wlr_output_layout_remove(server->output_layout, m->wlr_output);
	wlr_scene_output_destroy(m->scene_output);

	closemon(server, m);
	wlr_scene_node_destroy(&m->fullscreen_bg->node);
	free(m);
}

static void
requestmonstate(struct wl_listener *listener, void *data)
{
	Monitor *m = wl_container_of(listener, m, request_state);
	SwlServer *server = m->server;
	struct wlr_output_event_request_state *event = data;
	wlr_output_commit_state(event->output, event->state);
	swl_handle_layout_change(&server->layout_change, nullptr);
}

static void
outputmgrapplyortest(SwlServer *server,
		struct wlr_output_configuration_v1 *config, int test)
{
	/*
	 * Called when a client such as wlr-randr requests a change in output
	 * configuration. This is only one way that the layout can be changed,
	 * so any Monitor information should be updated by swl_handle_layout_change()
	 * after an output_layout.change event, not here.
	 */
	struct wlr_output_configuration_head_v1 *config_head;
	int ok = 1;

	wl_list_for_each(config_head, &config->heads, link) {
		struct wlr_output *wlr_output = config_head->state.output;
		Monitor *m = wlr_output->data;
		struct wlr_output_state state;

		/* Ensure displays previously disabled by wlr-output-power-management-v1
		 * are properly handled */
		m->asleep = false;

		wlr_output_state_init(&state);
		wlr_output_state_set_enabled(&state, config_head->state.enabled);
		if (!config_head->state.enabled)
			goto apply_or_test;

		if (config_head->state.mode)
			wlr_output_state_set_mode(&state, config_head->state.mode);
		else
			wlr_output_state_set_custom_mode(&state,
					config_head->state.custom_mode.width,
					config_head->state.custom_mode.height,
					config_head->state.custom_mode.refresh);

		wlr_output_state_set_transform(&state, config_head->state.transform);
		wlr_output_state_set_scale(&state, config_head->state.scale);
		wlr_output_state_set_adaptive_sync_enabled(&state,
				config_head->state.adaptive_sync_enabled);

apply_or_test:
		ok &= test ? wlr_output_test_state(wlr_output, &state)
				: wlr_output_commit_state(wlr_output, &state);

		/* Don't move monitors if position wouldn't change. This avoids
		 * wlroots marking the output as manually configured.
		 * wlr_output_layout_add does not like disabled outputs */
		if (!test && wlr_output->enabled && (m->m.x != config_head->state.x || m->m.y != config_head->state.y))
			wlr_output_layout_add(server->output_layout, wlr_output,
					config_head->state.x, config_head->state.y);

		wlr_output_state_finish(&state);
	}

	if (ok)
		wlr_output_configuration_v1_send_succeeded(config);
	else
		wlr_output_configuration_v1_send_failed(config);
	wlr_output_configuration_v1_destroy(config);

	/* https://codeberg.org/dwl/dwl/issues/577 */
	swl_handle_layout_change(&server->layout_change, nullptr);
}

