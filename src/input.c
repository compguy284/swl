#include <libinput.h>
#include <stdlib.h>
#include <wayland-server-core.h>
#include <wlr/backend/libinput.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_idle_notify_v1.h>
#include <wlr/interfaces/wlr_keyboard.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_keyboard_group.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_virtual_keyboard_v1.h>
#include <wlr/types/wlr_virtual_pointer_v1.h>
#include <xkbcommon/xkbcommon.h>

#include "input.h"
#include "macros.h"
#include "util.h"

static void createkeyboard(SwlServer *server, struct wlr_keyboard *keyboard);
static void createpointer(SwlServer *server, struct wlr_pointer *pointer);
static int keybinding(SwlServer *server, uint32_t mods, xkb_keysym_t sym);
static void keypress(struct wl_listener *listener, void *data);
static void keypressmod(struct wl_listener *listener, void *data);
static int keyrepeat(void *data);

static void
createkeyboard(SwlServer *server, struct wlr_keyboard *keyboard)
{
	wlr_keyboard_set_keymap(keyboard, server->kb_group->wlr_group->keyboard.keymap);

	if (server->locked_mods)
		wlr_keyboard_notify_modifiers(keyboard, 0, 0, server->locked_mods, 0);

	wlr_keyboard_group_add_keyboard(server->kb_group->wlr_group, keyboard);
}

static void
createpointer(SwlServer *server, struct wlr_pointer *pointer)
{
	struct libinput_device *device;
	if (wlr_input_device_is_libinput(&pointer->base)
			&& (device = wlr_libinput_get_device_handle(&pointer->base))) {
		if (libinput_device_config_tap_get_finger_count(device)) {
			libinput_device_config_tap_set_enabled(device, server->config.tap_to_click);
			libinput_device_config_tap_set_drag_enabled(device, server->config.tap_and_drag);
			libinput_device_config_tap_set_drag_lock_enabled(device, server->config.drag_lock);
			libinput_device_config_tap_set_button_map(device, server->config.button_map);
		}
		if (libinput_device_config_scroll_has_natural_scroll(device))
			libinput_device_config_scroll_set_natural_scroll_enabled(device, server->config.natural_scrolling);
		if (libinput_device_config_dwt_is_available(device))
			libinput_device_config_dwt_set_enabled(device, server->config.disable_while_typing);
		if (libinput_device_config_left_handed_is_available(device))
			libinput_device_config_left_handed_set(device, server->config.left_handed);
		if (libinput_device_config_middle_emulation_is_available(device))
			libinput_device_config_middle_emulation_set_enabled(device, server->config.middle_button_emulation);
		if (libinput_device_config_scroll_get_methods(device) != LIBINPUT_CONFIG_SCROLL_NO_SCROLL)
			libinput_device_config_scroll_set_method(device, server->config.scroll_method);
		if (libinput_device_config_click_get_methods(device) != LIBINPUT_CONFIG_CLICK_METHOD_NONE)
			libinput_device_config_click_set_method(device, server->config.click_method);
		if (libinput_device_config_send_events_get_modes(device))
			libinput_device_config_send_events_set_mode(device, server->config.send_events_mode);
		if (libinput_device_config_accel_is_available(device)) {
			libinput_device_config_accel_set_profile(device, server->config.accel_profile);
			libinput_device_config_accel_set_speed(device, server->config.accel_speed);
		}
	}
	wlr_cursor_attach_input_device(server->cursor, &pointer->base);
}

static int
keybinding(SwlServer *server, uint32_t mods, xkb_keysym_t sym)
{
	const Key *k;
	const Key *keys_end = server->config.keys + server->config.keys_count;
	for (k = server->config.keys; k < keys_end; k++) {
		if (CLEANMASK(mods) == CLEANMASK(k->mod)
				&& xkb_keysym_to_lower(sym) == xkb_keysym_to_lower(k->keysym)
				&& k->func) {
			k->func(server, &k->arg);
			return 1;
		}
	}
	return 0;
}

static void
keypress(struct wl_listener *listener, void *data)
{
	int i;
	KeyboardGroup *group = wl_container_of(listener, group, key);
	SwlServer *server = group->server;
	struct wlr_keyboard_key_event *event = data;
	uint32_t keycode = event->keycode + 8;
	const xkb_keysym_t *syms;
	int nsyms = xkb_state_key_get_syms(group->wlr_group->keyboard.xkb_state, keycode, &syms);
	int handled = 0;
	uint32_t mods = wlr_keyboard_get_modifiers(&group->wlr_group->keyboard);

	wlr_idle_notifier_v1_notify_activity(server->idle_notifier, server->seat);

	if (!server->locked && event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
		for (i = 0; i < nsyms; i++)
			handled = keybinding(server, mods, syms[i]) || handled;
	}

	if (handled && group->wlr_group->keyboard.repeat_info.delay > 0) {
		group->mods = mods;
		group->keysyms = syms;
		group->nsyms = nsyms;
		wl_event_source_timer_update(group->key_repeat_source,
				group->wlr_group->keyboard.repeat_info.delay);
	} else {
		group->nsyms = 0;
		wl_event_source_timer_update(group->key_repeat_source, 0);
	}

	if (handled)
		return;

	wlr_seat_set_keyboard(server->seat, &group->wlr_group->keyboard);
	wlr_seat_keyboard_notify_key(server->seat, event->time_msec,
			event->keycode, event->state);
}

static void
keypressmod(struct wl_listener *listener, void *data)
{
	KeyboardGroup *group = wl_container_of(listener, group, modifiers);
	SwlServer *server = group->server;
	wlr_seat_set_keyboard(server->seat, &group->wlr_group->keyboard);
	wlr_seat_keyboard_notify_modifiers(server->seat,
			&group->wlr_group->keyboard.modifiers);
}

static int
keyrepeat(void *data)
{
	KeyboardGroup *group = data;
	SwlServer *server = group->server;
	int i;

	if (!group->nsyms || group->wlr_group->keyboard.repeat_info.rate <= 0)
		return 0;

	wl_event_source_timer_update(group->key_repeat_source,
			1000 / group->wlr_group->keyboard.repeat_info.rate);

	for (i = 0; i < group->nsyms; i++)
		keybinding(server, group->mods, group->keysyms[i]);

	return 0;
}

void
swl_destroy_keyboard_group(struct wl_listener *listener, void *data)
{
	KeyboardGroup *group = wl_container_of(listener, group, destroy);
	wl_event_source_remove(group->key_repeat_source);
	wl_list_remove(&group->key.link);
	wl_list_remove(&group->modifiers.link);
	wl_list_remove(&group->destroy.link);
	wlr_keyboard_group_destroy(group->wlr_group);
	free(group);
}

KeyboardGroup *
swl_create_keyboard_group(SwlServer *server)
{
	KeyboardGroup *group = ecalloc(1, sizeof(*group));
	struct xkb_context *context;
	struct xkb_keymap *keymap;

	group->server = server;
	group->wlr_group = wlr_keyboard_group_create();
	group->wlr_group->data = group;

	context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	if (!(keymap = xkb_keymap_new_from_names(context, &server->config.xkb_rules,
			XKB_KEYMAP_COMPILE_NO_FLAGS)))
		die("failed to compile keymap");
	wlr_keyboard_set_keymap(&group->wlr_group->keyboard, keymap);
	xkb_keymap_unref(keymap);
	xkb_context_unref(context);

	wlr_keyboard_set_repeat_info(&group->wlr_group->keyboard,
			server->config.repeat_rate, server->config.repeat_delay);

	if (server->config.numlock) {
		xkb_mod_index_t mod = xkb_keymap_mod_get_index(
				group->wlr_group->keyboard.keymap, XKB_MOD_NAME_NUM);
		if (mod != XKB_MOD_INVALID) {
			server->locked_mods |= (uint32_t)1 << mod;
		}
	}

	if (server->locked_mods)
		wlr_keyboard_notify_modifiers(&group->wlr_group->keyboard,
				0, 0, server->locked_mods, 0);

	LISTEN(&group->wlr_group->keyboard.events.key, &group->key, keypress);
	LISTEN(&group->wlr_group->keyboard.events.modifiers, &group->modifiers, keypressmod);

	group->key_repeat_source = wl_event_loop_add_timer(server->event_loop,
			keyrepeat, group);

	wlr_seat_set_keyboard(server->seat, &group->wlr_group->keyboard);

	return group;
}

void
swl_handle_new_input(struct wl_listener *listener, void *data)
{
	SwlServer *server = wl_container_of(listener, server, new_input_device);
	struct wlr_input_device *device = data;
	uint32_t caps;

	switch (device->type) {
	case WLR_INPUT_DEVICE_KEYBOARD:
		createkeyboard(server, wlr_keyboard_from_input_device(device));
		break;
	case WLR_INPUT_DEVICE_POINTER:
		createpointer(server, wlr_pointer_from_input_device(device));
		break;
	default:
		break;
	}

	caps = WL_SEAT_CAPABILITY_POINTER;
	if (!wl_list_empty(&server->kb_group->wlr_group->devices))
		caps |= WL_SEAT_CAPABILITY_KEYBOARD;
	wlr_seat_set_capabilities(server->seat, caps);
}

void
swl_handle_new_virtual_keyboard(struct wl_listener *listener, void *data)
{
	SwlServer *server = wl_container_of(listener, server, new_virtual_keyboard);
	struct wlr_virtual_keyboard_v1 *kb = data;
	KeyboardGroup *group = swl_create_keyboard_group(server);

	wlr_keyboard_set_keymap(&kb->keyboard, group->wlr_group->keyboard.keymap);
	LISTEN(&kb->keyboard.base.events.destroy, &group->destroy, swl_destroy_keyboard_group);
	wlr_keyboard_group_add_keyboard(group->wlr_group, &kb->keyboard);
}

void
swl_handle_new_virtual_pointer(struct wl_listener *listener, void *data)
{
	SwlServer *server = wl_container_of(listener, server, new_virtual_pointer);
	struct wlr_virtual_pointer_v1_new_pointer_event *event = data;
	struct wlr_input_device *device = &event->new_pointer->pointer.base;

	wlr_cursor_attach_input_device(server->cursor, device);
	if (event->suggested_output)
		wlr_cursor_map_input_to_output(server->cursor, device,
				event->suggested_output);
}
