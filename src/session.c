#include <stdlib.h>
#include <scenefx/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_session_lock_v1.h>

#include "session.h"
#include "client.h"
#include "commands.h"
#include "cursor.h"
#include "macros.h"
#include "util.h"

static void createlocksurface(struct wl_listener *listener, void *data);
static void destroylocksurface(struct wl_listener *listener, void *data);
static void unlocksession(struct wl_listener *listener, void *data);
static void destroysessionlock(struct wl_listener *listener, void *data);
static void destroylock(SwlServer *server, SessionLock *lock, int unlock);

static void
createlocksurface(struct wl_listener *listener, void *data)
{
	SessionLock *lock = wl_container_of(listener, lock, new_surface);
	SwlServer *server = lock->server;
	struct wlr_session_lock_surface_v1 *lock_surface = data;
	Monitor *m = lock_surface->output->data;
	struct wlr_scene_tree *scene_tree = lock_surface->surface->data
			= wlr_scene_subsurface_tree_create(lock->scene, lock_surface->surface);
	m->lock_surface = lock_surface;

	wlr_scene_node_set_position(&scene_tree->node, m->m.x, m->m.y);
	wlr_session_lock_surface_v1_configure(lock_surface, m->m.width, m->m.height);

	LISTEN(&lock_surface->events.destroy, &m->destroy_lock_surface, destroylocksurface);

	if (m == server->selmon)
		swl_client_notify_enter(server->seat, lock_surface->surface,
				wlr_seat_get_keyboard(server->seat));
}

static void
destroylocksurface(struct wl_listener *listener, void *data)
{
	Monitor *m = wl_container_of(listener, m, destroy_lock_surface);
	SwlServer *server = m->server;
	struct wlr_session_lock_surface_v1 *surface, *lock_surface = m->lock_surface;

	m->lock_surface = nullptr;
	wl_list_remove(&m->destroy_lock_surface.link);

	if (lock_surface->surface != server->seat->keyboard_state.focused_surface)
		return;

	if (server->locked && server->cur_lock && !wl_list_empty(&server->cur_lock->surfaces)) {
		surface = wl_container_of(server->cur_lock->surfaces.next, surface, link);
		swl_client_notify_enter(server->seat, surface->surface,
				wlr_seat_get_keyboard(server->seat));
	} else if (!server->locked) {
		swl_focusclient(server, swl_focustop(server, server->selmon), 1);
	} else {
		wlr_seat_keyboard_clear_focus(server->seat);
	}
}

static void
destroylock(SwlServer *server, SessionLock *lock, int unlock)
{
	wlr_seat_keyboard_notify_clear_focus(server->seat);
	if ((server->locked = !unlock))
		goto destroy;

	wlr_scene_node_set_enabled(&server->locked_bg->node, 0);

	swl_focusclient(server, swl_focustop(server, server->selmon), 0);
	swl_motionnotify(server, 0, nullptr, 0, 0, 0, 0);

destroy:
	wl_list_remove(&lock->new_surface.link);
	wl_list_remove(&lock->unlock.link);
	wl_list_remove(&lock->destroy.link);

	wlr_scene_node_destroy(&lock->scene->node);
	server->cur_lock = nullptr;
	free(lock);
}

static void
destroysessionlock(struct wl_listener *listener, void *data)
{
	SessionLock *lock = wl_container_of(listener, lock, destroy);
	destroylock(lock->server, lock, 0);
}

static void
unlocksession(struct wl_listener *listener, void *data)
{
	SessionLock *lock = wl_container_of(listener, lock, unlock);
	destroylock(lock->server, lock, 1);
}

void
swl_handle_lock_session(struct wl_listener *listener, void *data)
{
	SwlServer *server = wl_container_of(listener, server, new_session_lock);
	struct wlr_session_lock_v1 *session_lock = data;
	SessionLock *lock;

	wlr_scene_node_set_enabled(&server->locked_bg->node, 1);
	if (server->cur_lock) {
		wlr_session_lock_v1_destroy(session_lock);
		return;
	}

	lock = session_lock->data = ecalloc(1, sizeof(*lock));
	lock->server = server;

	swl_focusclient(server, nullptr, 0);

	lock->scene = wlr_scene_tree_create(server->layers[LyrBlock]);
	server->cur_lock = lock->lock = session_lock;
	server->locked = true;

	LISTEN(&session_lock->events.new_surface, &lock->new_surface, createlocksurface);
	LISTEN(&session_lock->events.destroy, &lock->destroy, destroysessionlock);
	LISTEN(&session_lock->events.unlock, &lock->unlock, unlocksession);

	wlr_session_lock_v1_send_locked(session_lock);
}
