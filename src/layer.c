#include <stdlib.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <scenefx/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>

#include "layer.h"
#include "client.h"
#include "commands.h"
#include "cursor.h"
#include "layout.h"
#include "macros.h"
#include "util.h"

static constexpr int layermap[] = { LyrBg, LyrBottom, LyrTop, LyrOverlay };

static void arrangelayer(Monitor *m, struct wl_list *list, struct wlr_box *usable_area, int exclusive);
static void commitlayersurfacenotify(struct wl_listener *listener, void *data);
static void destroylayersurfacenotify(struct wl_listener *listener, void *data);
static void unmaplayersurfacenotify(struct wl_listener *listener, void *data);

static void
arrangelayer(Monitor *m, struct wl_list *list, struct wlr_box *usable_area, int exclusive)
{
	LayerSurface *l;
	struct wlr_box full_area = m->m;

	wl_list_for_each(l, list, link) {
		struct wlr_layer_surface_v1 *layer_surface = l->layer_surface;
		if (!layer_surface->initialized)
			continue;
		if (exclusive != (layer_surface->current.exclusive_zone > 0))
			continue;
		wlr_scene_layer_surface_v1_configure(l->scene_layer, &full_area, usable_area);
		wlr_scene_node_set_position(&l->popups->node, l->scene->node.x, l->scene->node.y);
	}
}

void
swl_arrangelayers(SwlServer *server, Monitor *m)
{
	int i;
	struct wlr_box usable_area = m->m;
	LayerSurface *l;
	uint32_t layers_above_shell[] = {
		ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
		ZWLR_LAYER_SHELL_V1_LAYER_TOP
	};

	if (!m->wlr_output->enabled)
		return;

	for (i = 3; i >= 0; i--)
		arrangelayer(m, &m->layers[i], &usable_area, 1);

	if (!wlr_box_equal(&usable_area, &m->w)) {
		m->w = usable_area;
		swl_arrange(server, m);
	}

	for (i = 3; i >= 0; i--)
		arrangelayer(m, &m->layers[i], &usable_area, 0);

	for (i = 0; i < (int)LENGTH(layers_above_shell); i++) {
		wl_list_for_each_reverse(l, &m->layers[layers_above_shell[i]], link) {
			if (server->locked || !l->layer_surface->current.keyboard_interactive
					|| !l->mapped)
				continue;
			swl_focusclient(server, nullptr, 0);
			server->exclusive_focus = l;
			swl_client_notify_enter(server->seat, l->layer_surface->surface,
					wlr_seat_get_keyboard(server->seat));
			return;
		}
	}
}

static void
commitlayersurfacenotify(struct wl_listener *listener, void *data)
{
	LayerSurface *l = wl_container_of(listener, l, surface_commit);
	SwlServer *server = l->server;
	struct wlr_layer_surface_v1 *layer_surface = l->layer_surface;
	struct wlr_scene_tree *scene_layer = server->layers[layermap[layer_surface->current.layer]];
	struct wlr_layer_surface_v1_state old_state;

	if (l->layer_surface->initial_commit) {
		swl_client_set_scale(layer_surface->surface, l->mon->wlr_output->scale);
		old_state = l->layer_surface->current;
		l->layer_surface->current = l->layer_surface->pending;
		swl_arrangelayers(server, l->mon);
		l->layer_surface->current = old_state;
		return;
	}

	if (layer_surface->current.committed == 0
			&& l->mapped == layer_surface->surface->mapped)
		return;

	l->mapped = layer_surface->surface->mapped;

	if (scene_layer != l->scene->node.parent) {
		wlr_scene_node_reparent(&l->scene->node, scene_layer);
		wl_list_remove(&l->link);
		wl_list_insert(&l->mon->layers[layer_surface->current.layer], &l->link);
		wlr_scene_node_reparent(&l->popups->node,
				(layer_surface->current.layer < ZWLR_LAYER_SHELL_V1_LAYER_TOP
				? server->layers[LyrTop] : scene_layer));
	}

	swl_arrangelayers(server, l->mon);
}

void
swl_handle_new_layer_surface(struct wl_listener *listener, void *data)
{
	SwlServer *server = wl_container_of(listener, server, new_layer_surface);
	struct wlr_layer_surface_v1 *layer_surface = data;
	LayerSurface *l;
	struct wlr_surface *surface = layer_surface->surface;
	struct wlr_scene_tree *scene_layer = server->layers[layermap[layer_surface->pending.layer]];

	if (!layer_surface->output
			&& !(layer_surface->output = server->selmon
			? server->selmon->wlr_output : nullptr)) {
		wlr_layer_surface_v1_destroy(layer_surface);
		return;
	}

	l = layer_surface->data = ecalloc(1, sizeof(*l));
	l->type = LayerShell;
	l->server = server;

	LISTEN(&surface->events.commit, &l->surface_commit, commitlayersurfacenotify);
	LISTEN(&surface->events.unmap, &l->unmap, unmaplayersurfacenotify);
	LISTEN(&layer_surface->events.destroy, &l->destroy, destroylayersurfacenotify);

	l->layer_surface = layer_surface;
	l->mon = layer_surface->output->data;
	l->scene_layer = wlr_scene_layer_surface_v1_create(scene_layer, layer_surface);
	l->scene = l->scene_layer->tree;
	l->popups = surface->data = wlr_scene_tree_create(
			layer_surface->current.layer < ZWLR_LAYER_SHELL_V1_LAYER_TOP
			? server->layers[LyrTop] : scene_layer);
	l->scene->node.data = l->popups->node.data = l;

	wl_list_insert(&l->mon->layers[layer_surface->pending.layer], &l->link);
	wlr_surface_send_enter(surface, layer_surface->output);
}

static void
destroylayersurfacenotify(struct wl_listener *listener, void *data)
{
	LayerSurface *l = wl_container_of(listener, l, destroy);

	wl_list_remove(&l->link);
	wl_list_remove(&l->destroy.link);
	wl_list_remove(&l->unmap.link);
	wl_list_remove(&l->surface_commit.link);
	wlr_scene_node_destroy(&l->scene->node);
	wlr_scene_node_destroy(&l->popups->node);
	free(l);
}

static void
unmaplayersurfacenotify(struct wl_listener *listener, void *data)
{
	LayerSurface *l = wl_container_of(listener, l, unmap);
	SwlServer *server = l->server;

	l->mapped = false;
	wlr_scene_node_set_enabled(&l->scene->node, 0);

	if (l == server->exclusive_focus)
		server->exclusive_focus = nullptr;

	if (l->layer_surface->output && (l->mon = l->layer_surface->output->data))
		swl_arrangelayers(server, l->mon);

	if (l->layer_surface->surface == server->seat->keyboard_state.focused_surface)
		swl_focusclient(server, swl_focustop(server, server->selmon), 1);

	swl_motionnotify(server, 0, nullptr, 0, 0, 0, 0);
}
