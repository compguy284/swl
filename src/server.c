#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/backend/session.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>

/* Forward-declare to avoid pulling in GLES2 headers from fx_renderer.h */
struct wlr_renderer *fx_renderer_create(struct wlr_backend *backend);
#include <wlr/types/wlr_alpha_modifier_v1.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_cursor_shape_v1.h>
#include <wlr/types/wlr_data_control_v1.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_drm.h>
#include <wlr/types/wlr_export_dmabuf_v1.h>
#include <wlr/types/wlr_ext_data_control_v1.h>
#include <wlr/types/wlr_fractional_scale_v1.h>
#include <wlr/types/wlr_gamma_control_v1.h>
#include <wlr/types/wlr_idle_inhibit_v1.h>
#include <wlr/types/wlr_idle_notify_v1.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_linux_dmabuf_v1.h>
#include <wlr/types/wlr_linux_drm_syncobj_v1.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_output_management_v1.h>
#include <wlr/types/wlr_output_power_management_v1.h>
#include <wlr/types/wlr_pointer_constraints_v1.h>
#include <wlr/types/wlr_presentation_time.h>
#include <wlr/types/wlr_primary_selection_v1.h>
#include <wlr/types/wlr_relative_pointer_v1.h>
#include <scenefx/types/wlr_scene.h>
#include <wlr/types/wlr_screencopy_v1.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_server_decoration.h>
#include <wlr/types/wlr_session_lock_v1.h>
#include <wlr/types/wlr_single_pixel_buffer_v1.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_viewporter.h>
#include <wlr/types/wlr_virtual_keyboard_v1.h>
#include <wlr/types/wlr_virtual_pointer_v1.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_activation_v1.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_xdg_output_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>
#ifdef XWAYLAND
#include <wlr/xwayland.h>
#endif

#include "server.h"
#include "clipboard.h"
#include "commands.h"
#include "cursor.h"
#include "idle.h"
#include "input.h"
#include "layer.h"
#include "layout.h"
#include "macros.h"
#include "output.h"
#include "session.h"
#include "util.h"
#ifdef XWAYLAND
#include "xwayland.h"
#endif

static int
handle_sighup(int signo, void *data)
{
	SwlServer *server = data;
	swl_cmd_reload_config(server, nullptr);
	return 0;
}

void
swl_apply_config_runtime(SwlServer *server)
{
	/* Log level */
	wlr_log_init(server->config.log_level, nullptr);

	/* Root background color */
	wlr_scene_rect_set_color(server->root_bg, server->config.rootcolor);

	/* Blur settings */
	if (server->config.blur_enabled)
		wlr_scene_set_blur_data(server->scene,
				server->config.blur_num_passes,
				server->config.blur_radius,
				server->config.blur_noise,
				server->config.blur_brightness,
				server->config.blur_contrast,
				server->config.blur_saturation);

	/* Keyboard config */
	swl_reapply_keyboard_config(server);

	/* Pointer/trackpad config */
	swl_reapply_pointer_config(server);

	/* Update fullscreen_bg color on all monitors */
	Monitor *m;
	wl_list_for_each(m, &server->mons, link)
		wlr_scene_rect_set_color(m->fullscreen_bg, server->config.fullscreen_bg);

	/* Client borders, shadows, corner radius */
	swl_reapply_client_config(server);

	/* Re-arrange all monitors */
	wl_list_for_each(m, &server->mons, link)
		swl_arrange(server, m);
}

void
swl_server_setup(SwlServer *server)
{
	int drm_fd, i, sig[] = {SIGCHLD, SIGINT, SIGTERM, SIGPIPE};
	struct sigaction sa = {.sa_flags = SA_RESTART, .sa_handler = swl_handlesig};
	sigemptyset(&sa.sa_mask);

	swl_commands_set_server(server);

	for (i = 0; i < (int)LENGTH(sig); i++)
		sigaction(sig[i], &sa, nullptr);

	wlr_log_init(server->config.log_level, nullptr);

	/* The Wayland display is managed by libwayland. It handles accepting
	 * clients from the Unix socket, managing Wayland globals, and so on. */
	server->dpy = wl_display_create();
	server->event_loop = wl_display_get_event_loop(server->dpy);

	/* SIGHUP triggers config reload via the event loop (safe, not a raw signal handler) */
	wl_event_loop_add_signal(server->event_loop, SIGHUP, handle_sighup, server);

	/* The backend is a wlroots feature which abstracts the underlying input and
	 * output hardware. The autocreate option will choose the most suitable
	 * backend based on the current environment, such as opening an X11 window
	 * if an X11 server is running. */
	if (!(server->backend = wlr_backend_autocreate(server->event_loop, &server->session)))
		die("couldn't create backend");

	/* Initialize the scene graph used to lay out windows */
	server->scene = wlr_scene_create();
	if (server->config.blur_enabled)
		wlr_scene_set_blur_data(server->scene,
				server->config.blur_num_passes,
				server->config.blur_radius,
				server->config.blur_noise,
				server->config.blur_brightness,
				server->config.blur_contrast,
				server->config.blur_saturation);
	server->root_bg = wlr_scene_rect_create(&server->scene->tree, 0, 0,
			server->config.rootcolor);
	for (unsigned int li = 0; li < NUM_LAYERS; li++)
		server->layers[li] = wlr_scene_tree_create(&server->scene->tree);
	server->drag_icon = wlr_scene_tree_create(&server->scene->tree);
	wlr_scene_node_place_below(&server->drag_icon->node,
			&server->layers[LyrBlock]->node);

	/* Create the SceneFX renderer (GLES2-based with effects support).
	 * The renderer is responsible for defining the various pixel formats it
	 * supports for shared memory, this configures that for clients. */
	if (!(server->drw = fx_renderer_create(server->backend)))
		die("couldn't create fx renderer");
	wl_signal_add(&server->drw->events.lost, &server->gpu_reset);
	server->gpu_reset.notify = swl_handle_gpu_reset;

	/* Create shm, drm and linux_dmabuf interfaces by ourselves.
	 * The simplest way is to call:
	 *      wlr_renderer_init_wl_display(drw);
	 * but we need to create the linux_dmabuf interface manually to integrate it
	 * with wlr_scene. */
	wlr_renderer_init_wl_shm(server->drw, server->dpy);

	if (wlr_renderer_get_texture_formats(server->drw, WLR_BUFFER_CAP_DMABUF)) {
		wlr_drm_create(server->dpy, server->drw);
		wlr_scene_set_linux_dmabuf_v1(server->scene,
				wlr_linux_dmabuf_v1_create_with_renderer(server->dpy, 5, server->drw));
	}

	if ((drm_fd = wlr_renderer_get_drm_fd(server->drw)) >= 0
			&& server->drw->features.timeline
			&& server->backend->features.timeline)
		wlr_linux_drm_syncobj_manager_v1_create(server->dpy, 1, drm_fd);

	/* Autocreates an allocator for us.
	 * The allocator is the bridge between the renderer and the backend. It
	 * handles the buffer creation, allowing wlroots to render onto the
	 * screen */
	if (!(server->alloc = wlr_allocator_autocreate(server->backend, server->drw)))
		die("couldn't create allocator");

	/* This creates some hands-off wlroots interfaces. The compositor is
	 * necessary for clients to allocate surfaces and the data device manager
	 * handles the clipboard. Each of these wlroots interfaces has room for you
	 * to dig your fingers in and play with their behavior if you want. Note that
	 * the clients cannot set the selection directly without compositor approval,
	 * see the setsel() function. */
	server->compositor = wlr_compositor_create(server->dpy, 6, server->drw);
	wlr_subcompositor_create(server->dpy);
	wlr_data_device_manager_create(server->dpy);
	wlr_export_dmabuf_manager_v1_create(server->dpy);
	wlr_screencopy_manager_v1_create(server->dpy);
	wlr_data_control_manager_v1_create(server->dpy);
	wlr_ext_data_control_manager_v1_create(server->dpy, 1);
	wlr_primary_selection_v1_device_manager_create(server->dpy);
	wlr_viewporter_create(server->dpy);
	wlr_single_pixel_buffer_manager_v1_create(server->dpy);
	wlr_fractional_scale_manager_v1_create(server->dpy, 1);
	wlr_presentation_create(server->dpy, server->backend, 2);
	wlr_alpha_modifier_v1_create(server->dpy);

	/* Initializes the interface used to implement urgency hints */
	server->activation = wlr_xdg_activation_v1_create(server->dpy);

	wlr_scene_set_gamma_control_manager_v1(server->scene,
			wlr_gamma_control_manager_v1_create(server->dpy));

	server->power_mgr = wlr_output_power_manager_v1_create(server->dpy);
	LISTEN(&server->power_mgr->events.set_mode,
			&server->output_power_mgr_set_mode, swl_handle_output_power_set_mode);

	/* Creates an output layout, which is a wlroots utility for working with an
	 * arrangement of screens in a physical layout. */
	server->output_layout = wlr_output_layout_create(server->dpy);
	LISTEN(&server->output_layout->events.change,
			&server->layout_change, swl_handle_layout_change);

	wlr_xdg_output_manager_v1_create(server->dpy, server->output_layout);

	/* Configure a listener to be notified when new outputs are available on the
	 * backend. */
	wl_list_init(&server->mons);
	LISTEN(&server->backend->events.new_output,
			&server->new_output, swl_handle_new_output);

	/* Set up our client lists, the xdg-shell and the layer-shell. */
	wl_list_init(&server->clients);
	wl_list_init(&server->fstack);
	wl_list_init(&server->pointers);

	server->xdg_shell = wlr_xdg_shell_create(server->dpy, 6);

	server->layer_shell = wlr_layer_shell_v1_create(server->dpy, 3);
	LISTEN(&server->layer_shell->events.new_surface,
			&server->new_layer_surface, swl_handle_new_layer_surface);

	server->idle_notifier = wlr_idle_notifier_v1_create(server->dpy);

	server->idle_inhibit_mgr = wlr_idle_inhibit_v1_create(server->dpy);
	LISTEN(&server->idle_inhibit_mgr->events.new_inhibitor,
			&server->new_idle_inhibitor, swl_handle_new_idle_inhibitor);

	server->session_lock_mgr = wlr_session_lock_manager_v1_create(server->dpy);
	LISTEN(&server->session_lock_mgr->events.new_lock,
			&server->new_session_lock, swl_handle_lock_session);
	server->locked_bg = wlr_scene_rect_create(server->layers[LyrBlock],
			server->sgeom.width, server->sgeom.height,
			(float [4]){0.1f, 0.1f, 0.1f, 1.0f});
	wlr_scene_node_set_enabled(&server->locked_bg->node, 0);

	/* Use decoration protocols to negotiate server-side decorations */
	wlr_server_decoration_manager_set_default_mode(
			wlr_server_decoration_manager_create(server->dpy),
			WLR_SERVER_DECORATION_MANAGER_MODE_SERVER);
	server->xdg_decoration_mgr = wlr_xdg_decoration_manager_v1_create(server->dpy);
	server->foreign_toplevel_mgr = wlr_foreign_toplevel_manager_v1_create(server->dpy);

	/* Wire up xdg-shell, xdg-decoration, and activation listeners (in commands module) */
	swl_commands_setup_listeners(server);

	server->pointer_constraints = wlr_pointer_constraints_v1_create(server->dpy);
	LISTEN(&server->pointer_constraints->events.new_constraint,
			&server->new_pointer_constraint, swl_handle_new_pointer_constraint);

	server->relative_pointer_mgr = wlr_relative_pointer_manager_v1_create(server->dpy);

	/*
	 * Creates a cursor, which is a wlroots utility for tracking the cursor
	 * image shown on screen.
	 */
	server->cursor = wlr_cursor_create();
	wlr_cursor_attach_output_layout(server->cursor, server->output_layout);

	/* Creates an xcursor manager, another wlroots utility which loads up
	 * Xcursor themes to source cursor images from and makes sure that cursor
	 * images are available at all scale factors on the screen (necessary for
	 * HiDPI support). Scaled cursors will be loaded with each output. */
	server->cursor_mgr = wlr_xcursor_manager_create(nullptr, 24);
	setenv("XCURSOR_SIZE", "24", 1);

	/*
	 * wlr_cursor *only* displays an image on screen. It does not move around
	 * when the pointer moves. However, we can attach input devices to it, and
	 * it will generate aggregate events for all of them. In these events, we
	 * can choose how we want to process them, forwarding them to clients and
	 * moving the cursor around.
	 */
	LISTEN(&server->cursor->events.motion,
			&server->cursor_motion, swl_handle_cursor_motion);
	LISTEN(&server->cursor->events.motion_absolute,
			&server->cursor_motion_absolute, swl_handle_cursor_motion_absolute);
	LISTEN(&server->cursor->events.button,
			&server->cursor_button, swl_handle_cursor_button);
	LISTEN(&server->cursor->events.axis,
			&server->cursor_axis, swl_handle_cursor_axis);
	LISTEN(&server->cursor->events.frame,
			&server->cursor_frame, swl_handle_cursor_frame);

	server->cursor_shape_mgr = wlr_cursor_shape_manager_v1_create(server->dpy, 1);
	LISTEN(&server->cursor_shape_mgr->events.request_set_shape,
			&server->request_set_cursor_shape, swl_handle_set_cursor_shape);

	/*
	 * Configures a seat, which is a single "seat" at which a user sits and
	 * operates the computer. This conceptually includes up to one keyboard,
	 * pointer, touch, and drawing tablet device. We also rig up a listener to
	 * let us know when new input devices are available on the backend.
	 */
	LISTEN(&server->backend->events.new_input,
			&server->new_input_device, swl_handle_new_input);
	server->virtual_keyboard_mgr = wlr_virtual_keyboard_manager_v1_create(server->dpy);
	LISTEN(&server->virtual_keyboard_mgr->events.new_virtual_keyboard,
			&server->new_virtual_keyboard, swl_handle_new_virtual_keyboard);
	server->virtual_pointer_mgr = wlr_virtual_pointer_manager_v1_create(server->dpy);
	LISTEN(&server->virtual_pointer_mgr->events.new_virtual_pointer,
			&server->new_virtual_pointer, swl_handle_new_virtual_pointer);

	server->seat = wlr_seat_create(server->dpy, "seat0");
	LISTEN(&server->seat->events.request_set_cursor,
			&server->request_cursor, swl_handle_set_cursor);
	LISTEN(&server->seat->events.request_set_selection,
			&server->request_set_sel, swl_handle_request_set_sel);
	LISTEN(&server->seat->events.request_set_primary_selection,
			&server->request_set_psel, swl_handle_request_set_psel);
	LISTEN(&server->seat->events.request_start_drag,
			&server->request_start_drag, swl_handle_request_start_drag);
	LISTEN(&server->seat->events.start_drag,
			&server->start_drag, swl_handle_start_drag);

	server->kb_group = swl_create_keyboard_group(server);
	wl_list_init(&server->kb_group->destroy.link);

	server->output_mgr = wlr_output_manager_v1_create(server->dpy);
	LISTEN(&server->output_mgr->events.apply,
			&server->output_mgr_apply, swl_handle_output_mgr_apply);
	LISTEN(&server->output_mgr->events.test,
			&server->output_mgr_test, swl_handle_output_mgr_test);

	/* Make sure XWayland clients don't connect to the parent X server,
	 * e.g when running in the x11 backend or the wayland backend and the
	 * compositor has Xwayland support */
	unsetenv("DISPLAY");
#ifdef XWAYLAND
	/*
	 * Initialise the XWayland X server.
	 * It will be started when the first X client is started.
	 */
	if ((server->xwayland = wlr_xwayland_create(server->dpy, server->compositor, 1))) {
		LISTEN(&server->xwayland->events.ready,
				&server->xwayland_ready, swl_handle_xwayland_ready);
		LISTEN(&server->xwayland->events.new_surface,
				&server->new_xwayland_surface, swl_handle_new_xwayland_surface);

		setenv("DISPLAY", server->xwayland->display_name, 1);
	} else {
		fprintf(stderr, "failed to setup XWayland X server, continuing without it\n");
	}
#endif
}

static void
set_session_env(void)
{
	setenv("XDG_CURRENT_DESKTOP", "swl", 0);
	pid_t pid = fork();
	if (pid == 0) {
		setsid();
		execlp("dbus-update-activation-environment",
		       "dbus-update-activation-environment", "--systemd",
		       "WAYLAND_DISPLAY", "DISPLAY", "XDG_CURRENT_DESKTOP", "PATH",
		       (char *)NULL);
		_exit(1);
	}
	if (pid > 0)
		waitpid(pid, NULL, 0);
}

static void
start_session_target(void)
{
	pid_t pid = fork();
	if (pid == 0) {
		setsid();
		execlp("systemctl", "systemctl", "--user", "start",
		       "--no-block", "swl-session.target", (char *)NULL);
		_exit(1);
	}
}

static void
stop_session_target(void)
{
	pid_t pid = fork();
	if (pid == 0) {
		execlp("systemctl", "systemctl", "--user", "stop",
		       "swl-session.target", (char *)NULL);
		_exit(1);
	}
	if (pid > 0)
		waitpid(pid, NULL, 0);
}

void
swl_server_run(SwlServer *server, char *startup_cmd)
{
	/* Add a Unix socket to the Wayland display. */
	const char *socket = wl_display_add_socket_auto(server->dpy);
	if (!socket)
		die("startup: display_add_socket_auto");
	setenv("WAYLAND_DISPLAY", socket, 1);

	/* Start the backend. This will enumerate outputs and inputs, become the DRM
	 * master, etc */
	if (!wlr_backend_start(server->backend))
		die("startup: backend_start");

	/* Export session env to D-Bus/systemd and start session target */
	set_session_env();
	start_session_target();

	/* Now that the socket exists and the backend is started, run the startup command */
	if (startup_cmd) {
		int piperw[2];
		if (pipe(piperw) < 0)
			die("startup: pipe:");
		if ((server->child_pid = fork()) < 0)
			die("startup: fork:");
		if (server->child_pid == 0) {
			setsid();
			dup2(piperw[0], STDIN_FILENO);
			close(piperw[0]);
			close(piperw[1]);
			execl("/bin/sh", "/bin/sh", "-c", startup_cmd, nullptr);
			die("startup: execl:");
		}
		dup2(piperw[1], STDOUT_FILENO);
		close(piperw[1]);
		close(piperw[0]);
	}

	/* Mark stdout as non-blocking to avoid the startup script
	 * causing swl to freeze when a user neither closes stdin
	 * nor consumes standard input in his startup script */
	if (fd_set_nonblock(STDOUT_FILENO) < 0)
		close(STDOUT_FILENO);

	swl_printstatus(server);

	/* At this point the outputs are initialized, choose initial selmon based on
	 * cursor position, and set default cursor image */
	server->selmon = swl_xytomon(server, server->cursor->x, server->cursor->y);

	/* TODO hack to get cursor to display in its initial location (100, 100)
	 * instead of (0, 0) and then jumping. Still may not be fully
	 * initialized, as the image/coordinates are not transformed for the
	 * monitor when displayed here */
	wlr_cursor_warp_closest(server->cursor, nullptr, server->cursor->x, server->cursor->y);
	wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "default");

	/* Run the Wayland event loop. This does not return until you exit the
	 * compositor. Starting the backend rigged up all of the necessary event
	 * loop configuration to listen to libinput events, DRM events, generate
	 * frame events at the refresh rate, and so on. */
	wl_display_run(server->dpy);
}

static void
cleanuplisteners(SwlServer *server)
{
	wl_list_remove(&server->cursor_axis.link);
	wl_list_remove(&server->cursor_button.link);
	wl_list_remove(&server->cursor_frame.link);
	wl_list_remove(&server->cursor_motion.link);
	wl_list_remove(&server->cursor_motion_absolute.link);
	wl_list_remove(&server->gpu_reset.link);
	wl_list_remove(&server->new_idle_inhibitor.link);
	wl_list_remove(&server->layout_change.link);
	wl_list_remove(&server->new_input_device.link);
	wl_list_remove(&server->new_virtual_keyboard.link);
	wl_list_remove(&server->new_virtual_pointer.link);
	wl_list_remove(&server->new_pointer_constraint.link);
	wl_list_remove(&server->new_output.link);
	wl_list_remove(&server->new_xdg_toplevel.link);
	wl_list_remove(&server->new_xdg_decoration.link);
	wl_list_remove(&server->new_xdg_popup.link);
	wl_list_remove(&server->new_layer_surface.link);
	wl_list_remove(&server->output_mgr_apply.link);
	wl_list_remove(&server->output_mgr_test.link);
	wl_list_remove(&server->output_power_mgr_set_mode.link);
	wl_list_remove(&server->request_activate.link);
	wl_list_remove(&server->request_cursor.link);
	wl_list_remove(&server->request_set_psel.link);
	wl_list_remove(&server->request_set_sel.link);
	wl_list_remove(&server->request_set_cursor_shape.link);
	wl_list_remove(&server->request_start_drag.link);
	wl_list_remove(&server->start_drag.link);
	wl_list_remove(&server->new_session_lock.link);
#ifdef XWAYLAND
	wl_list_remove(&server->new_xwayland_surface.link);
	wl_list_remove(&server->xwayland_ready.link);
#endif
}

void
swl_server_cleanup(SwlServer *server)
{
	stop_session_target();
	cleanuplisteners(server);
#ifdef XWAYLAND
	wlr_xwayland_destroy(server->xwayland);
	server->xwayland = nullptr;
#endif
	wl_display_destroy_clients(server->dpy);
	if (server->child_pid > 0) {
		/* Reset SIGCHLD to default to prevent the handler from reaping
		 * child_pid before we can waitpid for it */
		signal(SIGCHLD, SIG_DFL);
		kill(-server->child_pid, SIGTERM);
		/* Poll with timeout, escalate to SIGKILL if child won't die */
		for (int attempts = 0; attempts < 50; attempts++) {
			if (waitpid(server->child_pid, nullptr, WNOHANG) != 0)
				goto reaped;
			nanosleep(&(struct timespec){.tv_nsec = 20000000}, nullptr); /* 20ms, up to 1s total */
		}
		kill(-server->child_pid, SIGKILL);
		waitpid(server->child_pid, nullptr, 0);
	reaped:;
	}
	wlr_xcursor_manager_destroy(server->cursor_mgr);

	swl_destroy_keyboard_group(&server->kb_group->destroy, nullptr);

	/* If it's not destroyed manually, it will cause a use-after-free of wlr_seat.
	 * Destroy it until it's fixed on the wlroots side */
	wlr_backend_destroy(server->backend);

	wl_display_destroy(server->dpy);
	/* Destroy after the wayland display (when the monitors are already destroyed)
	   to avoid destroying them with an invalid scene output. */
	wlr_scene_node_destroy(&server->scene->tree.node);

	swl_config_free(&server->config);
	free(server->config_path);
}
