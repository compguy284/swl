#ifndef SWL_SERVER_H
#define SWL_SERVER_H

#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_cursor_shape_v1.h>
#include <wlr/types/wlr_idle_inhibit_v1.h>
#include <wlr/types/wlr_idle_notify_v1.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_output_management_v1.h>
#include <wlr/types/wlr_output_power_management_v1.h>
#include <wlr/types/wlr_pointer_constraints_v1.h>
#include <wlr/types/wlr_relative_pointer_v1.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_session_lock_v1.h>
#include <wlr/types/wlr_virtual_keyboard_v1.h>
#include <wlr/types/wlr_virtual_pointer_v1.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_activation_v1.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
#ifdef XWAYLAND
#include <wlr/xwayland.h>
#endif

#include "types.h"
#include "config.h"

struct SwlServer {
	/* Core wlroots objects */
	struct wl_display *dpy;
	struct wl_event_loop *event_loop;
	struct wlr_backend *backend;
	struct wlr_scene *scene;
	struct wlr_renderer *drw;
	struct wlr_allocator *alloc;
	struct wlr_compositor *compositor;
	struct wlr_session *session;

	/* Scene layers */
	struct wlr_scene_tree *layers[NUM_LAYERS];
	struct wlr_scene_tree *drag_icon;
	struct wlr_scene_rect *root_bg;

	/* Shells */
	struct wlr_xdg_shell *xdg_shell;
	struct wlr_layer_shell_v1 *layer_shell;
	struct wlr_xdg_activation_v1 *activation;
	struct wlr_xdg_decoration_manager_v1 *xdg_decoration_mgr;

	/* Managers */
	struct wlr_idle_notifier_v1 *idle_notifier;
	struct wlr_idle_inhibit_manager_v1 *idle_inhibit_mgr;
	struct wlr_output_manager_v1 *output_mgr;
	struct wlr_cursor_shape_manager_v1 *cursor_shape_mgr;
	struct wlr_output_power_manager_v1 *power_mgr;
	struct wlr_virtual_keyboard_manager_v1 *virtual_keyboard_mgr;
	struct wlr_virtual_pointer_manager_v1 *virtual_pointer_mgr;
	struct wlr_session_lock_manager_v1 *session_lock_mgr;
	struct wlr_pointer_constraints_v1 *pointer_constraints;
	struct wlr_relative_pointer_manager_v1 *relative_pointer_mgr;

	/* Input */
	struct wlr_seat *seat;
	KeyboardGroup *kb_group;

	/* Cursor state */
	struct wlr_cursor *cursor;
	struct wlr_xcursor_manager *cursor_mgr;
	unsigned int cursor_mode;
	Client *grabc;
	int grabcx, grabcy; /* client-relative */
	struct wlr_pointer_constraint_v1 *active_constraint;

	/* Output */
	struct wlr_output_layout *output_layout;
	struct wlr_box sgeom;
	struct wl_list mons; /* Monitor.link */
	Monitor *selmon;

	/* Session lock */
	struct wlr_scene_rect *locked_bg;
	struct wlr_session_lock_v1 *cur_lock;
	bool locked;

	/* Client lists */
	struct wl_list clients; /* Client.link — tiling order */
	struct wl_list fstack;  /* Client.flink — focus order */

	/* Misc */
	pid_t child_pid;
	void *exclusive_focus;

	/* Event listeners */
	struct wl_listener cursor_axis;
	struct wl_listener cursor_button;
	struct wl_listener cursor_frame;
	struct wl_listener cursor_motion;
	struct wl_listener cursor_motion_absolute;
	struct wl_listener gpu_reset;
	struct wl_listener layout_change;
	struct wl_listener new_idle_inhibitor;
	struct wl_listener new_input_device;
	struct wl_listener new_virtual_keyboard;
	struct wl_listener new_virtual_pointer;
	struct wl_listener new_pointer_constraint;
	struct wl_listener new_output;
	struct wl_listener new_xdg_toplevel;
	struct wl_listener new_xdg_popup;
	struct wl_listener new_xdg_decoration;
	struct wl_listener new_layer_surface;
	struct wl_listener output_mgr_apply;
	struct wl_listener output_mgr_test;
	struct wl_listener output_power_mgr_set_mode;
	struct wl_listener request_activate;
	struct wl_listener request_cursor;
	struct wl_listener request_set_psel;
	struct wl_listener request_set_sel;
	struct wl_listener request_set_cursor_shape;
	struct wl_listener request_start_drag;
	struct wl_listener start_drag;
	struct wl_listener new_session_lock;

#ifdef XWAYLAND
	struct wlr_xwayland *xwayland;
	struct wl_listener new_xwayland_surface;
	struct wl_listener xwayland_ready;
#endif

	/* Configuration */
	SwlConfig config;
};

void swl_server_setup(SwlServer *server);
void swl_server_run(SwlServer *server, char *startup_cmd);
void swl_server_cleanup(SwlServer *server);

#endif
