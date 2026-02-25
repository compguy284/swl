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
	char **lid_close_cmd; /* NULL-terminated argv, or nullptr */
	unsigned int borderpx;
	float rootcolor[4];
	float bordercolor[4];
	float focuscolor[4];
	float urgentcolor[4];
	float fullscreen_bg[4];

	/* Logging */
	int log_level;

	/* Keyboard (XKB) */
	struct xkb_rule_names xkb_rules;
	int repeat_rate;
	int repeat_delay;
	bool numlock;

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

	/* Monitor rules */
	const MonitorRule *monrules;
	size_t monrules_count;

	/* Key bindings */
	const Key *keys;
	size_t keys_count;

	/* Mouse bindings */
	const Button *buttons;
	size_t buttons_count;

	/* Scroller layout */
	float scroller_default_width;
	float *scroller_preset_widths;
	size_t scroller_preset_count;
	enum SwlScrollerCenter scroller_center;
	bool scroller_center_single;
	int gap_width;

	/* Effects (scenefx) */
	int corner_radius;
	float opacity;

	bool shadow_enabled;
	float shadow_sigma;
	float shadow_color[4];

	bool blur_enabled;
	int blur_num_passes;
	int blur_radius;
	float blur_noise;
	float blur_brightness;
	float blur_contrast;
	float blur_saturation;
} SwlConfig;

void swl_config_defaults(SwlConfig *config);
[[nodiscard]] int swl_config_load(SwlConfig *config, const char *path);

#endif
