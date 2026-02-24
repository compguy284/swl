#ifndef SWL_CONFIG_H
#define SWL_CONFIG_H

#include <stdbool.h>
#include <libinput.h>
#include <wlr/util/log.h>
#include <xkbcommon/xkbcommon.h>
#include "types.h"

typedef struct SwlConfig {
	/* Appearance */
	bool sloppyfocus;
	bool bypass_surface_visibility;
	unsigned int borderpx;
	float rootcolor[4];
	float bordercolor[4];
	float focuscolor[4];
	float urgentcolor[4];
	float fullscreen_bg[4];

	/* Tags */
	int tag_count;

	/* Logging */
	int log_level;

	/* Keyboard (XKB) */
	struct xkb_rule_names xkb_rules;
	int repeat_rate;
	int repeat_delay;

	/* Trackpad/pointer (libinput) */
	bool tap_to_click;
	bool tap_and_drag;
	bool drag_lock;
	bool natural_scrolling;
	bool disable_while_typing;
	bool left_handed;
	bool middle_button_emulation;
	enum libinput_config_scroll_method scroll_method;
	enum libinput_config_click_method click_method;
	uint32_t send_events_mode;
	enum libinput_config_accel_profile accel_profile;
	double accel_speed;
	enum libinput_config_tap_button_map button_map;

	/* Rules */
	const Rule *rules;
	size_t rules_count;

	/* Layouts */
	const Layout *layouts;
	size_t layouts_count;

	/* Monitor rules */
	const MonitorRule *monrules;
	size_t monrules_count;

	/* Key bindings */
	const Key *keys;
	size_t keys_count;

	/* Mouse bindings */
	const Button *buttons;
	size_t buttons_count;
} SwlConfig;

void swl_config_defaults(SwlConfig *config);
[[nodiscard]] int swl_config_load(SwlConfig *config, const char *path);

#endif
