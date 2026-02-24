#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libinput.h>
#include <linux/input-event-codes.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/util/log.h>
#include <xkbcommon/xkbcommon.h>
#include "toml.h"

#include "config.h"
#include "commands.h"
#include "layout.h"
#include "macros.h"
#include "scroller.h"

/* Taken from https://github.com/djpohly/dwl/issues/466 */
#define COLOR(hex)    { ((hex >> 24) & 0xFF) / 255.0f, \
                        ((hex >> 16) & 0xFF) / 255.0f, \
                        ((hex >> 8) & 0xFF) / 255.0f, \
                        (hex & 0xFF) / 255.0f }

/* If you want to use the windows key for MODKEY, use WLR_MODIFIER_LOGO */
#define MODKEY WLR_MODIFIER_ALT

#define TAGKEYS(KEY,SKEY,TAG) \
	{ MODKEY,                    KEY,            swl_cmd_view,            {.ui = 1 << TAG} }, \
	{ MODKEY|WLR_MODIFIER_CTRL,  KEY,            swl_cmd_toggleview,      {.ui = 1 << TAG} }, \
	{ MODKEY|WLR_MODIFIER_SHIFT, SKEY,           swl_cmd_tag,             {.ui = 1 << TAG} }, \
	{ MODKEY|WLR_MODIFIER_CTRL|WLR_MODIFIER_SHIFT,SKEY,swl_cmd_toggletag, {.ui = 1 << TAG} }

#define CHVT(n) { WLR_MODIFIER_CTRL|WLR_MODIFIER_ALT,XKB_KEY_XF86Switch_VT_##n, swl_cmd_chvt, {.ui = (n)} }

static const Rule default_rules[] = {
	/* app_id             title       tags mask     isfloating   monitor */
	{ "Gimp_EXAMPLE",     nullptr,       0,            1,           -1 },
	{ "firefox_EXAMPLE",  nullptr,       1 << 8,       0,           -1 },
};

static const Layout default_layouts[] = {
	/* symbol     arrange function */
	{ "[]=",      swl_tile },
	{ "><>",      nullptr },    /* no layout function means floating behavior */
	{ "[M]",      swl_monocle },
};

static const MonitorRule default_monrules[] = {
	/* name       mfact  nmaster scale layout              rotate/reflect                x    y */
	{ nullptr,       0.55f, 1,      1,    &default_layouts[0], WL_OUTPUT_TRANSFORM_NORMAL,   -1,  -1 },
};

/* commands */
static const char *termcmd[] = { "foot", nullptr };
static const char *menucmd[] = { "wmenu-run", nullptr };

static const Key default_keys[] = {
	/* modifier                  key                  function                argument */
	{ MODKEY,                    XKB_KEY_p,           swl_cmd_spawn,          {.v = menucmd} },
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_Return,      swl_cmd_spawn,          {.v = termcmd} },
	{ MODKEY,                    XKB_KEY_j,           swl_cmd_focusstack,     {.i = +1} },
	{ MODKEY,                    XKB_KEY_k,           swl_cmd_focusstack,     {.i = -1} },
	{ MODKEY,                    XKB_KEY_i,           swl_cmd_incnmaster,     {.i = +1} },
	{ MODKEY,                    XKB_KEY_d,           swl_cmd_incnmaster,     {.i = -1} },
	{ MODKEY,                    XKB_KEY_h,           swl_cmd_setmfact,       {.f = -0.05f} },
	{ MODKEY,                    XKB_KEY_l,           swl_cmd_setmfact,       {.f = +0.05f} },
	{ MODKEY,                    XKB_KEY_Return,      swl_cmd_zoom,           {0} },
	{ MODKEY,                    XKB_KEY_Tab,         swl_cmd_view,           {0} },
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_c,           swl_cmd_killclient,     {0} },
	{ MODKEY,                    XKB_KEY_t,           swl_cmd_setlayout,      {.v = &default_layouts[0]} },
	{ MODKEY,                    XKB_KEY_f,           swl_cmd_setlayout,      {.v = &default_layouts[1]} },
	{ MODKEY,                    XKB_KEY_m,           swl_cmd_setlayout,      {.v = &default_layouts[2]} },
	{ MODKEY,                    XKB_KEY_space,       swl_cmd_setlayout,      {0} },
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_space,       swl_cmd_togglefloating, {0} },
	{ MODKEY,                    XKB_KEY_e,           swl_cmd_togglefullscreen, {0} },
	{ MODKEY,                    XKB_KEY_0,           swl_cmd_view,           {.ui = ~0u} },
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_parenright,  swl_cmd_tag,            {.ui = ~0u} },
	{ MODKEY,                    XKB_KEY_comma,       swl_cmd_focusmon,       {.i = WLR_DIRECTION_LEFT} },
	{ MODKEY,                    XKB_KEY_period,      swl_cmd_focusmon,       {.i = WLR_DIRECTION_RIGHT} },
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_less,        swl_cmd_tagmon,         {.i = WLR_DIRECTION_LEFT} },
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_greater,     swl_cmd_tagmon,         {.i = WLR_DIRECTION_RIGHT} },
	TAGKEYS(          XKB_KEY_1, XKB_KEY_exclam,                              0),
	TAGKEYS(          XKB_KEY_2, XKB_KEY_at,                                  1),
	TAGKEYS(          XKB_KEY_3, XKB_KEY_numbersign,                          2),
	TAGKEYS(          XKB_KEY_4, XKB_KEY_dollar,                              3),
	TAGKEYS(          XKB_KEY_5, XKB_KEY_percent,                             4),
	TAGKEYS(          XKB_KEY_6, XKB_KEY_asciicircum,                         5),
	TAGKEYS(          XKB_KEY_7, XKB_KEY_ampersand,                           6),
	TAGKEYS(          XKB_KEY_8, XKB_KEY_asterisk,                            7),
	TAGKEYS(          XKB_KEY_9, XKB_KEY_parenleft,                           8),
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_q,           swl_cmd_quit,           {0} },

	/* Ctrl-Alt-Backspace and Ctrl-Alt-Fx used to be handled by X server */
	{ WLR_MODIFIER_CTRL|WLR_MODIFIER_ALT,XKB_KEY_Terminate_Server, swl_cmd_quit, {0} },
	CHVT(1), CHVT(2), CHVT(3), CHVT(4), CHVT(5), CHVT(6),
	CHVT(7), CHVT(8), CHVT(9), CHVT(10), CHVT(11), CHVT(12),
};

static const Button default_buttons[] = {
	{ MODKEY, BTN_LEFT,   swl_cmd_moveresize,     {.ui = CurMove} },
	{ MODKEY, BTN_MIDDLE, swl_cmd_togglefloating, {0} },
	{ MODKEY, BTN_RIGHT,  swl_cmd_moveresize,     {.ui = CurResize} },
};

void
swl_config_defaults(SwlConfig *config)
{
	/* Appearance */
	config->sloppyfocus = true;
	config->bypass_surface_visibility = false;
	config->borderpx = 1;
	static constexpr float rc[] = COLOR(0x222222ff);
	static constexpr float bc[] = COLOR(0x444444ff);
	static constexpr float fc[] = COLOR(0x005577ff);
	static constexpr float uc[] = COLOR(0xff0000ff);
	static constexpr float fb[] = {0.0f, 0.0f, 0.0f, 1.0f};
	for (int i = 0; i < 4; i++) {
		config->rootcolor[i] = rc[i];
		config->bordercolor[i] = bc[i];
		config->focuscolor[i] = fc[i];
		config->urgentcolor[i] = uc[i];
		config->fullscreen_bg[i] = fb[i];
	}

	/* Tags */
	config->tag_count = 9;

	/* Logging */
	config->log_level = WLR_ERROR;

	/* Keyboard */
	config->xkb_rules = (struct xkb_rule_names){
		.options = nullptr,
	};
	config->repeat_rate = 25;
	config->repeat_delay = 600;

	/* Trackpad */
	config->tap_to_click = true;
	config->tap_and_drag = true;
	config->drag_lock = true;
	config->natural_scrolling = false;
	config->disable_while_typing = true;
	config->left_handed = false;
	config->middle_button_emulation = false;
	config->scroll_method = LIBINPUT_CONFIG_SCROLL_2FG;
	config->click_method = LIBINPUT_CONFIG_CLICK_METHOD_BUTTON_AREAS;
	config->send_events_mode = LIBINPUT_CONFIG_SEND_EVENTS_ENABLED;
	config->accel_profile = LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE;
	config->accel_speed = 0.0;
	config->button_map = LIBINPUT_CONFIG_TAP_MAP_LRM;

	/* Rules */
	config->rules = default_rules;
	config->rules_count = LENGTH(default_rules);

	/* Layouts */
	config->layouts = default_layouts;
	config->layouts_count = LENGTH(default_layouts);

	/* Monitor rules */
	config->monrules = default_monrules;
	config->monrules_count = LENGTH(default_monrules);

	/* Key bindings */
	config->keys = default_keys;
	config->keys_count = LENGTH(default_keys);

	/* Mouse bindings */
	config->buttons = default_buttons;
	config->buttons_count = LENGTH(default_buttons);

	/* Scroller layout */
	config->scroller_default_width = 0.5f;
	static float default_presets[] = { 0.5f, 0.67f, 1.0f };
	config->scroller_preset_widths = default_presets;
	config->scroller_preset_count = LENGTH(default_presets);
	config->scroller_center = ScrollCenterNever;
	config->scroller_center_single = false;
}

/* =====================================================================
 * TOML configuration loading
 * ===================================================================== */

/* --------------- Helper: parse "0xRRGGBBAA" into float[4] -------------- */
static int
parse_color(const char *hex, float out[4])
{
	if (!hex || hex[0] != '0' || (hex[1] != 'x' && hex[1] != 'X'))
		return -1;

	char *end;
	unsigned long val = strtoul(hex, &end, 16);
	if (*end != '\0')
		return -1;

	out[0] = ((val >> 24) & 0xFF) / 255.0f;
	out[1] = ((val >> 16) & 0xFF) / 255.0f;
	out[2] = ((val >> 8)  & 0xFF) / 255.0f;
	out[3] = ((val)       & 0xFF) / 255.0f;
	return 0;
}

/* --------------- Lookup tables ---------------------------------------- */

typedef void (*SwlCmdFunc)(SwlServer *, const Arg *);

static const struct { const char *name; SwlCmdFunc func; } action_funcs[] = {
	{ "spawn",            swl_cmd_spawn },
	{ "killclient",       swl_cmd_killclient },
	{ "focusstack",       swl_cmd_focusstack },
	{ "focusmon",         swl_cmd_focusmon },
	{ "incnmaster",       swl_cmd_incnmaster },
	{ "setmfact",         swl_cmd_setmfact },
	{ "setlayout",        swl_cmd_setlayout },
	{ "togglefloating",   swl_cmd_togglefloating },
	{ "togglefullscreen", swl_cmd_togglefullscreen },
	{ "view",             swl_cmd_view },
	{ "tag",              swl_cmd_tag },
	{ "tagmon",           swl_cmd_tagmon },
	{ "toggletag",        swl_cmd_toggletag },
	{ "toggleview",       swl_cmd_toggleview },
	{ "zoom",             swl_cmd_zoom },
	{ "moveresize",       swl_cmd_moveresize },
	{ "quit",             swl_cmd_quit },
	{ "chvt",             swl_cmd_chvt },
	{ "scroller_cycle_width", swl_cmd_scroller_cycle_width },
	{ "scroller_set_width", swl_cmd_scroller_set_width },
};

static const struct { const char *name; void (*func)(SwlServer *, Monitor *); } arrange_funcs[] = {
	{ "tile",    swl_tile },
	{ "monocle", swl_monocle },
	{ "scroller", swl_scroller },
};

static const struct { const char *name; uint32_t mod; } mod_names[] = {
	{ "shift", WLR_MODIFIER_SHIFT },
	{ "ctrl",  WLR_MODIFIER_CTRL },
	{ "alt",   WLR_MODIFIER_ALT },
	{ "super", WLR_MODIFIER_LOGO },
};

static const struct { const char *name; unsigned int btn; } button_names[] = {
	{ "left",   BTN_LEFT },
	{ "middle", BTN_MIDDLE },
	{ "right",  BTN_RIGHT },
};

/* Shift-key symbols for tag number keys 1-9 (US layout) */
static constexpr xkb_keysym_t tag_shift_syms[] = {
	XKB_KEY_exclam,      /* 1 -> ! */
	XKB_KEY_at,          /* 2 -> @ */
	XKB_KEY_numbersign,  /* 3 -> # */
	XKB_KEY_dollar,      /* 4 -> $ */
	XKB_KEY_percent,     /* 5 -> % */
	XKB_KEY_asciicircum, /* 6 -> ^ */
	XKB_KEY_ampersand,   /* 7 -> & */
	XKB_KEY_asterisk,    /* 8 -> * */
	XKB_KEY_parenleft,   /* 9 -> ( */
};

/* --------------- Commands storage (file-scope) ----------------------- */

/* Named command arrays parsed from [commands] section */
typedef struct {
	char  *name;
	char **argv; /* NULL-terminated */
} NamedCommand;

static NamedCommand *named_cmds = nullptr;
static size_t named_cmds_count = 0;

static char **
find_command(const char *name)
{
	for (size_t i = 0; i < named_cmds_count; i++) {
		if (strcmp(named_cmds[i].name, name) == 0)
			return named_cmds[i].argv;
	}
	return nullptr;
}

/* --------------- Action lookup --------------------------------------- */

static SwlCmdFunc
find_action(const char *name)
{
	for (size_t i = 0; i < LENGTH(action_funcs); i++) {
		if (strcmp(action_funcs[i].name, name) == 0)
			return action_funcs[i].func;
	}
	return nullptr;
}

/* --------------- Mod key resolution ---------------------------------- */

static uint32_t modkey_value = WLR_MODIFIER_ALT; /* default */

static uint32_t
resolve_mods(toml_array_t *arr)
{
	uint32_t mods = 0;
	int n = toml_array_nelem(arr);
	for (int i = 0; i < n; i++) {
		toml_datum_t d = toml_string_at(arr, i);
		if (!d.ok) continue;
		if (strcmp(d.u.s, "mod") == 0) {
			mods |= modkey_value;
		} else if (strcmp(d.u.s, "ctrl_alt") == 0) {
			mods |= WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT;
		} else {
			for (size_t j = 0; j < LENGTH(mod_names); j++) {
				if (strcmp(d.u.s, mod_names[j].name) == 0) {
					mods |= mod_names[j].mod;
					break;
				}
			}
		}
		free(d.u.s);
	}
	return mods;
}

/* --------------- Find layout by symbol name -------------------------- */

static const Layout *
find_layout(const Layout *layouts, size_t count, const char *symbol)
{
	for (size_t i = 0; i < count; i++) {
		if (strcmp(layouts[i].symbol, symbol) == 0)
			return &layouts[i];
	}
	return nullptr;
}

/* --------------- Parse arg for a given action ------------------------ */

static Arg
parse_arg(const char *action, toml_table_t *tab, const Layout *layouts,
          size_t layouts_count)
{
	Arg arg = {0};

	if (strcmp(action, "spawn") == 0) {
		toml_datum_t d = toml_string_in(tab, "arg");
		if (d.ok) {
			char **cmd = find_command(d.u.s);
			if (cmd)
				arg.v = cmd;
			else
				fprintf(stderr, "swl_config_load: unknown command '%s'\n", d.u.s);
			free(d.u.s);
		}
	} else if (strcmp(action, "focusstack") == 0 || strcmp(action, "incnmaster") == 0
	           || strcmp(action, "scroller_cycle_width") == 0) {
		toml_datum_t d = toml_int_in(tab, "arg");
		if (d.ok)
			arg.i = (int)d.u.i;
	} else if (strcmp(action, "setmfact") == 0) {
		toml_datum_t d = toml_double_in(tab, "arg");
		if (d.ok)
			arg.f = (float)d.u.d;
	} else if (strcmp(action, "scroller_set_width") == 0) {
		toml_datum_t d = toml_double_in(tab, "arg");
		if (d.ok)
			arg.f = (float)d.u.d;
	} else if (strcmp(action, "view") == 0 || strcmp(action, "tag") == 0 ||
	           strcmp(action, "toggletag") == 0 || strcmp(action, "toggleview") == 0) {
		/* Can be int or "all" */
		toml_datum_t ds = toml_string_in(tab, "arg");
		if (ds.ok) {
			if (strcmp(ds.u.s, "all") == 0)
				arg.ui = ~0u;
			free(ds.u.s);
		} else {
			toml_datum_t di = toml_int_in(tab, "arg");
			if (di.ok)
				arg.ui = (uint32_t)di.u.i;
		}
	} else if (strcmp(action, "setlayout") == 0) {
		toml_datum_t d = toml_string_in(tab, "arg");
		if (d.ok) {
			const Layout *lt = find_layout(layouts, layouts_count, d.u.s);
			if (lt)
				arg.v = lt;
			else
				fprintf(stderr, "swl_config_load: unknown layout '%s'\n", d.u.s);
			free(d.u.s);
		}
	} else if (strcmp(action, "focusmon") == 0 || strcmp(action, "tagmon") == 0) {
		toml_datum_t d = toml_string_in(tab, "arg");
		if (d.ok) {
			if (strcmp(d.u.s, "left") == 0)
				arg.i = WLR_DIRECTION_LEFT;
			else if (strcmp(d.u.s, "right") == 0)
				arg.i = WLR_DIRECTION_RIGHT;
			free(d.u.s);
		}
	} else if (strcmp(action, "moveresize") == 0) {
		toml_datum_t d = toml_string_in(tab, "arg");
		if (d.ok) {
			if (strcmp(d.u.s, "move") == 0)
				arg.ui = CurMove;
			else if (strcmp(d.u.s, "resize") == 0)
				arg.ui = CurResize;
			free(d.u.s);
		}
	} else if (strcmp(action, "chvt") == 0) {
		toml_datum_t d = toml_int_in(tab, "arg");
		if (d.ok)
			arg.ui = (uint32_t)d.u.i;
	}

	return arg;
}

/* --------------- Section parsers ------------------------------------- */

static void
parse_general(toml_table_t *tab, SwlConfig *config)
{
	toml_datum_t d;

	d = toml_bool_in(tab, "sloppy_focus");
	if (d.ok) config->sloppyfocus = d.u.b;

	d = toml_bool_in(tab, "bypass_surface_visibility");
	if (d.ok) config->bypass_surface_visibility = d.u.b;

	d = toml_int_in(tab, "tag_count");
	if (d.ok) config->tag_count = (int)d.u.i;
}

static void
parse_appearance(toml_table_t *tab, SwlConfig *config)
{
	toml_datum_t d;

	d = toml_int_in(tab, "border_width");
	if (d.ok) config->borderpx = (unsigned int)d.u.i;

	d = toml_string_in(tab, "root_color");
	if (d.ok) { parse_color(d.u.s, config->rootcolor); free(d.u.s); }

	d = toml_string_in(tab, "border_color");
	if (d.ok) { parse_color(d.u.s, config->bordercolor); free(d.u.s); }

	d = toml_string_in(tab, "focus_color");
	if (d.ok) { parse_color(d.u.s, config->focuscolor); free(d.u.s); }

	d = toml_string_in(tab, "urgent_color");
	if (d.ok) { parse_color(d.u.s, config->urgentcolor); free(d.u.s); }

	d = toml_string_in(tab, "fullscreen_bg");
	if (d.ok) { parse_color(d.u.s, config->fullscreen_bg); free(d.u.s); }
}

static void
parse_keyboard(toml_table_t *tab, SwlConfig *config)
{
	toml_datum_t d;

	d = toml_int_in(tab, "repeat_rate");
	if (d.ok) config->repeat_rate = (int)d.u.i;

	d = toml_int_in(tab, "repeat_delay");
	if (d.ok) config->repeat_delay = (int)d.u.i;

	d = toml_string_in(tab, "rules");
	if (d.ok) { config->xkb_rules.rules = strdup(d.u.s); free(d.u.s); }

	d = toml_string_in(tab, "model");
	if (d.ok) { config->xkb_rules.model = strdup(d.u.s); free(d.u.s); }

	d = toml_string_in(tab, "layout");
	if (d.ok) { config->xkb_rules.layout = strdup(d.u.s); free(d.u.s); }

	d = toml_string_in(tab, "variant");
	if (d.ok) { config->xkb_rules.variant = strdup(d.u.s); free(d.u.s); }

	d = toml_string_in(tab, "options");
	if (d.ok) { config->xkb_rules.options = strdup(d.u.s); free(d.u.s); }
}

static void
parse_trackpad(toml_table_t *tab, SwlConfig *config)
{
	toml_datum_t d;

	d = toml_bool_in(tab, "tap_to_click");
	if (d.ok) config->tap_to_click = d.u.b;

	d = toml_bool_in(tab, "tap_and_drag");
	if (d.ok) config->tap_and_drag = d.u.b;

	d = toml_bool_in(tab, "drag_lock");
	if (d.ok) config->drag_lock = d.u.b;

	d = toml_bool_in(tab, "natural_scrolling");
	if (d.ok) config->natural_scrolling = d.u.b;

	d = toml_bool_in(tab, "disable_while_typing");
	if (d.ok) config->disable_while_typing = d.u.b;

	d = toml_bool_in(tab, "left_handed");
	if (d.ok) config->left_handed = d.u.b;

	d = toml_bool_in(tab, "middle_button_emulation");
	if (d.ok) config->middle_button_emulation = d.u.b;

	d = toml_string_in(tab, "scroll_method");
	if (d.ok) {
		if (strcmp(d.u.s, "no_scroll") == 0)
			config->scroll_method = LIBINPUT_CONFIG_SCROLL_NO_SCROLL;
		else if (strcmp(d.u.s, "two_finger") == 0)
			config->scroll_method = LIBINPUT_CONFIG_SCROLL_2FG;
		else if (strcmp(d.u.s, "edge") == 0)
			config->scroll_method = LIBINPUT_CONFIG_SCROLL_EDGE;
		else if (strcmp(d.u.s, "on_button") == 0)
			config->scroll_method = LIBINPUT_CONFIG_SCROLL_ON_BUTTON_DOWN;
		free(d.u.s);
	}

	d = toml_string_in(tab, "click_method");
	if (d.ok) {
		if (strcmp(d.u.s, "none") == 0)
			config->click_method = LIBINPUT_CONFIG_CLICK_METHOD_NONE;
		else if (strcmp(d.u.s, "button_areas") == 0)
			config->click_method = LIBINPUT_CONFIG_CLICK_METHOD_BUTTON_AREAS;
		else if (strcmp(d.u.s, "clickfinger") == 0)
			config->click_method = LIBINPUT_CONFIG_CLICK_METHOD_CLICKFINGER;
		free(d.u.s);
	}

	d = toml_string_in(tab, "accel_profile");
	if (d.ok) {
		if (strcmp(d.u.s, "flat") == 0)
			config->accel_profile = LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT;
		else if (strcmp(d.u.s, "adaptive") == 0)
			config->accel_profile = LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE;
		free(d.u.s);
	}

	d = toml_double_in(tab, "accel_speed");
	if (d.ok) config->accel_speed = d.u.d;

	d = toml_string_in(tab, "send_events");
	if (d.ok) {
		if (strcmp(d.u.s, "enabled") == 0)
			config->send_events_mode = LIBINPUT_CONFIG_SEND_EVENTS_ENABLED;
		else if (strcmp(d.u.s, "disabled") == 0)
			config->send_events_mode = LIBINPUT_CONFIG_SEND_EVENTS_DISABLED;
		else if (strcmp(d.u.s, "disabled_on_external_mouse") == 0)
			config->send_events_mode = LIBINPUT_CONFIG_SEND_EVENTS_DISABLED_ON_EXTERNAL_MOUSE;
		free(d.u.s);
	}

	d = toml_string_in(tab, "button_map");
	if (d.ok) {
		if (strcmp(d.u.s, "lrm") == 0)
			config->button_map = LIBINPUT_CONFIG_TAP_MAP_LRM;
		else if (strcmp(d.u.s, "lmr") == 0)
			config->button_map = LIBINPUT_CONFIG_TAP_MAP_LMR;
		free(d.u.s);
	}
}

static void
parse_scroller(toml_table_t *tab, SwlConfig *config)
{
	toml_datum_t d;

	d = toml_double_in(tab, "default_column_width");
	if (d.ok) config->scroller_default_width = (float)d.u.d;

	d = toml_bool_in(tab, "always_center_single_column");
	if (d.ok) config->scroller_center_single = d.u.b;

	d = toml_string_in(tab, "center_focused_column");
	if (d.ok) {
		if (strcmp(d.u.s, "always") == 0)
			config->scroller_center = ScrollCenterAlways;
		else if (strcmp(d.u.s, "on-overflow") == 0)
			config->scroller_center = ScrollCenterOverflow;
		else
			config->scroller_center = ScrollCenterNever;
		free(d.u.s);
	}

	toml_array_t *arr = toml_array_in(tab, "preset_column_widths");
	if (arr) {
		int n = toml_array_nelem(arr);
		if (n > 0) {
			float *widths = calloc((size_t)n, sizeof(float));
			if (widths) {
				int count = 0;
				for (int i = 0; i < n; i++) {
					toml_datum_t v = toml_double_at(arr, i);
					if (v.ok)
						widths[count++] = (float)v.u.d;
				}
				config->scroller_preset_widths = widths;
				config->scroller_preset_count = (size_t)count;
			}
		}
	}
}

static int
parse_rules(toml_array_t *arr, SwlConfig *config)
{
	int n = toml_array_nelem(arr);
	if (n == 0) return 0;

	Rule *rules = calloc((size_t)n, sizeof(Rule));
	if (!rules) return -1;

	int count = 0;
	for (int i = 0; i < n; i++) {
		toml_table_t *entry = toml_table_at(arr, i);
		if (!entry) continue;

		Rule *r = &rules[count];

		toml_datum_t d;
		d = toml_string_in(entry, "app_id");
		r->id = d.ok ? strdup(d.u.s) : nullptr;
		if (d.ok) free(d.u.s);

		d = toml_string_in(entry, "title");
		r->title = d.ok ? strdup(d.u.s) : nullptr;
		if (d.ok) free(d.u.s);

		d = toml_int_in(entry, "tags");
		r->tags = d.ok ? (uint32_t)d.u.i : 0;

		d = toml_bool_in(entry, "floating");
		r->isfloating = d.ok ? d.u.b : false;

		d = toml_int_in(entry, "monitor");
		r->monitor = d.ok ? (int)d.u.i : -1;

		count++;
	}

	config->rules = rules;
	config->rules_count = (size_t)count;
	return 0;
}

static int
parse_layouts(toml_array_t *arr, SwlConfig *config)
{
	int n = toml_array_nelem(arr);
	if (n == 0) return 0;

	Layout *layouts = calloc((size_t)n, sizeof(Layout));
	if (!layouts) return -1;

	int count = 0;
	for (int i = 0; i < n; i++) {
		toml_table_t *entry = toml_table_at(arr, i);
		if (!entry) continue;

		toml_datum_t sym = toml_string_in(entry, "symbol");
		if (!sym.ok) {
			fprintf(stderr, "swl_config_load: layout[%d] missing 'symbol'\n", i);
			continue;
		}

		Layout *lt = &layouts[count];
		lt->symbol = strdup(sym.u.s);
		free(sym.u.s);

		lt->arrange = nullptr;
		toml_datum_t ar = toml_string_in(entry, "arrange");
		if (ar.ok) {
			for (size_t j = 0; j < LENGTH(arrange_funcs); j++) {
				if (strcmp(ar.u.s, arrange_funcs[j].name) == 0) {
					lt->arrange = arrange_funcs[j].func;
					break;
				}
			}
			free(ar.u.s);
		}

		count++;
	}

	config->layouts = layouts;
	config->layouts_count = (size_t)count;
	return 0;
}

static enum wl_output_transform
parse_transform(const char *str)
{
	if (strcmp(str, "normal") == 0)      return WL_OUTPUT_TRANSFORM_NORMAL;
	if (strcmp(str, "90") == 0)          return WL_OUTPUT_TRANSFORM_90;
	if (strcmp(str, "180") == 0)         return WL_OUTPUT_TRANSFORM_180;
	if (strcmp(str, "270") == 0)         return WL_OUTPUT_TRANSFORM_270;
	if (strcmp(str, "flipped") == 0)     return WL_OUTPUT_TRANSFORM_FLIPPED;
	if (strcmp(str, "flipped_90") == 0)  return WL_OUTPUT_TRANSFORM_FLIPPED_90;
	if (strcmp(str, "flipped_180") == 0) return WL_OUTPUT_TRANSFORM_FLIPPED_180;
	if (strcmp(str, "flipped_270") == 0) return WL_OUTPUT_TRANSFORM_FLIPPED_270;
	return WL_OUTPUT_TRANSFORM_NORMAL;
}

static int
parse_monitors(toml_array_t *arr, SwlConfig *config)
{
	int n = toml_array_nelem(arr);
	if (n == 0) return 0;

	MonitorRule *monrules = calloc((size_t)n, sizeof(MonitorRule));
	if (!monrules) return -1;

	int count = 0;
	for (int i = 0; i < n; i++) {
		toml_table_t *entry = toml_table_at(arr, i);
		if (!entry) continue;

		MonitorRule *mr = &monrules[count];
		toml_datum_t d;

		d = toml_string_in(entry, "name");
		mr->name = d.ok ? strdup(d.u.s) : nullptr;
		if (d.ok) free(d.u.s);

		d = toml_double_in(entry, "mfact");
		mr->mfact = d.ok ? (float)d.u.d : 0.55f;

		d = toml_int_in(entry, "nmaster");
		mr->nmaster = d.ok ? (int)d.u.i : 1;

		d = toml_double_in(entry, "scale");
		mr->scale = d.ok ? (float)d.u.d : 1.0f;

		/* Resolve layout by symbol name; default to first layout */
		mr->lt = (config->layouts_count > 0) ? &config->layouts[0] : nullptr;
		d = toml_string_in(entry, "layout");
		if (d.ok) {
			const Layout *lt = find_layout(config->layouts, config->layouts_count, d.u.s);
			if (lt)
				mr->lt = lt;
			else
				fprintf(stderr, "swl_config_load: monitor[%d] unknown layout '%s'\n", i, d.u.s);
			free(d.u.s);
		}

		d = toml_string_in(entry, "transform");
		mr->rr = d.ok ? parse_transform(d.u.s) : WL_OUTPUT_TRANSFORM_NORMAL;
		if (d.ok) free(d.u.s);

		d = toml_int_in(entry, "x");
		mr->x = d.ok ? (int)d.u.i : -1;

		d = toml_int_in(entry, "y");
		mr->y = d.ok ? (int)d.u.i : -1;

		count++;
	}

	config->monrules = monrules;
	config->monrules_count = (size_t)count;
	return 0;
}

static void
parse_commands(toml_table_t *tab)
{
	/* Parse mod_key */
	toml_datum_t mk = toml_string_in(tab, "mod_key");
	if (mk.ok) {
		if (strcmp(mk.u.s, "super") == 0)
			modkey_value = WLR_MODIFIER_LOGO;
		else if (strcmp(mk.u.s, "alt") == 0)
			modkey_value = WLR_MODIFIER_ALT;
		free(mk.u.s);
	}

	/* Count named commands (string arrays in table, excluding mod_key) */
	/* We iterate known command names; extensible by checking all keys */
	const char *cmd_names[] = { "terminal", "menu" };
	size_t ncmd_names = LENGTH(cmd_names);

	/* Count how many are actually present */
	size_t present = 0;
	for (size_t i = 0; i < ncmd_names; i++) {
		toml_array_t *arr = toml_array_in(tab, cmd_names[i]);
		if (arr) present++;
	}

	if (present == 0) return;

	named_cmds = calloc(present, sizeof(NamedCommand));
	named_cmds_count = 0;

	for (size_t i = 0; i < ncmd_names; i++) {
		toml_array_t *arr = toml_array_in(tab, cmd_names[i]);
		if (!arr) continue;

		int n = toml_array_nelem(arr);
		char **argv = calloc((size_t)(n + 1), sizeof(char *));
		if (!argv) continue;

		for (int j = 0; j < n; j++) {
			toml_datum_t d = toml_string_at(arr, j);
			argv[j] = d.ok ? strdup(d.u.s) : strdup("");
			if (d.ok) free(d.u.s);
		}
		argv[n] = nullptr;

		named_cmds[named_cmds_count].name = strdup(cmd_names[i]);
		named_cmds[named_cmds_count].argv = argv;
		named_cmds_count++;
	}
}

static int
parse_key_bindings(toml_array_t *arr, SwlConfig *config)
{
	int n = toml_array_nelem(arr);

	/*
	 * Extra keys needed:
	 * - 4 TAGKEYS per tag (view, toggleview, tag, toggletag)
	 * - 1 Ctrl+Alt+Backspace -> quit
	 * - 12 CHVT bindings (F1-F12)
	 */
	int tag_count = config->tag_count;
	int extra = tag_count * 4 + 1 + 12;
	int total = n + extra;

	Key *keys = calloc((size_t)total, sizeof(Key));
	if (!keys) return -1;

	int count = 0;

	/* Parse user-defined keybinds */
	for (int i = 0; i < n; i++) {
		toml_table_t *entry = toml_table_at(arr, i);
		if (!entry) continue;

		toml_datum_t action_d = toml_string_in(entry, "action");
		if (!action_d.ok) {
			fprintf(stderr, "swl_config_load: bind.key[%d] missing 'action'\n", i);
			continue;
		}
		SwlCmdFunc func = find_action(action_d.u.s);
		if (!func) {
			fprintf(stderr, "swl_config_load: bind.key[%d] unknown action '%s'\n", i, action_d.u.s);
			free(action_d.u.s);
			continue;
		}

		toml_datum_t key_d = toml_string_in(entry, "key");
		if (!key_d.ok) {
			fprintf(stderr, "swl_config_load: bind.key[%d] missing 'key'\n", i);
			free(action_d.u.s);
			continue;
		}
		xkb_keysym_t keysym = xkb_keysym_from_name(key_d.u.s, XKB_KEYSYM_NO_FLAGS);
		if (keysym == XKB_KEY_NoSymbol) {
			fprintf(stderr, "swl_config_load: bind.key[%d] unknown key '%s'\n", i, key_d.u.s);
			free(key_d.u.s);
			free(action_d.u.s);
			continue;
		}
		free(key_d.u.s);

		uint32_t mods = 0;
		toml_array_t *mods_arr = toml_array_in(entry, "mods");
		if (mods_arr)
			mods = resolve_mods(mods_arr);

		Arg arg = parse_arg(action_d.u.s, entry, config->layouts, config->layouts_count);
		free(action_d.u.s);

		keys[count].mod = mods;
		keys[count].keysym = keysym;
		keys[count].func = func;
		keys[count].arg = arg;
		count++;
	}

	/* Append TAGKEYS for each tag */
	for (int t = 0; t < tag_count && t < 9; t++) {
		xkb_keysym_t num_key = XKB_KEY_1 + (xkb_keysym_t)t;
		xkb_keysym_t shift_key = tag_shift_syms[t];

		/* MOD + num -> view */
		keys[count].mod = modkey_value;
		keys[count].keysym = num_key;
		keys[count].func = swl_cmd_view;
		keys[count].arg = (Arg){.ui = 1u << t};
		count++;

		/* MOD + CTRL + num -> toggleview */
		keys[count].mod = modkey_value | WLR_MODIFIER_CTRL;
		keys[count].keysym = num_key;
		keys[count].func = swl_cmd_toggleview;
		keys[count].arg = (Arg){.ui = 1u << t};
		count++;

		/* MOD + SHIFT + shift_sym -> tag */
		keys[count].mod = modkey_value | WLR_MODIFIER_SHIFT;
		keys[count].keysym = shift_key;
		keys[count].func = swl_cmd_tag;
		keys[count].arg = (Arg){.ui = 1u << t};
		count++;

		/* MOD + CTRL + SHIFT + shift_sym -> toggletag */
		keys[count].mod = modkey_value | WLR_MODIFIER_CTRL | WLR_MODIFIER_SHIFT;
		keys[count].keysym = shift_key;
		keys[count].func = swl_cmd_toggletag;
		keys[count].arg = (Arg){.ui = 1u << t};
		count++;
	}

	/* Ctrl+Alt+Backspace -> quit */
	keys[count].mod = WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT;
	keys[count].keysym = XKB_KEY_Terminate_Server;
	keys[count].func = swl_cmd_quit;
	keys[count].arg = (Arg){0};
	count++;

	/* CHVT: Ctrl+Alt+F1-F12 */
	for (int vt = 1; vt <= 12; vt++) {
		keys[count].mod = WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT;
		keys[count].keysym = XKB_KEY_XF86Switch_VT_1 + (xkb_keysym_t)(vt - 1);
		keys[count].func = swl_cmd_chvt;
		keys[count].arg = (Arg){.ui = (uint32_t)vt};
		count++;
	}

	config->keys = keys;
	config->keys_count = (size_t)count;
	return 0;
}

static int
parse_mouse_bindings(toml_array_t *arr, SwlConfig *config)
{
	int n = toml_array_nelem(arr);
	if (n == 0) return 0;

	Button *buttons = calloc((size_t)n, sizeof(Button));
	if (!buttons) return -1;

	int count = 0;
	for (int i = 0; i < n; i++) {
		toml_table_t *entry = toml_table_at(arr, i);
		if (!entry) continue;

		toml_datum_t action_d = toml_string_in(entry, "action");
		if (!action_d.ok) {
			fprintf(stderr, "swl_config_load: bind.mouse[%d] missing 'action'\n", i);
			continue;
		}
		SwlCmdFunc func = find_action(action_d.u.s);
		if (!func) {
			fprintf(stderr, "swl_config_load: bind.mouse[%d] unknown action '%s'\n", i, action_d.u.s);
			free(action_d.u.s);
			continue;
		}

		toml_datum_t btn_d = toml_string_in(entry, "button");
		if (!btn_d.ok) {
			fprintf(stderr, "swl_config_load: bind.mouse[%d] missing 'button'\n", i);
			free(action_d.u.s);
			continue;
		}
		unsigned int button = 0;
		for (size_t j = 0; j < LENGTH(button_names); j++) {
			if (strcmp(btn_d.u.s, button_names[j].name) == 0) {
				button = button_names[j].btn;
				break;
			}
		}
		if (button == 0) {
			fprintf(stderr, "swl_config_load: bind.mouse[%d] unknown button '%s'\n", i, btn_d.u.s);
			free(btn_d.u.s);
			free(action_d.u.s);
			continue;
		}
		free(btn_d.u.s);

		uint32_t mods = 0;
		toml_array_t *mods_arr = toml_array_in(entry, "mods");
		if (mods_arr)
			mods = resolve_mods(mods_arr);

		Arg arg = parse_arg(action_d.u.s, entry, config->layouts, config->layouts_count);
		free(action_d.u.s);

		buttons[count].mod = mods;
		buttons[count].button = button;
		buttons[count].func = func;
		buttons[count].arg = arg;
		count++;
	}

	config->buttons = buttons;
	config->buttons_count = (size_t)count;
	return 0;
}

/* --------------- Main entry point ------------------------------------ */

int
swl_config_load(SwlConfig *config, const char *path)
{
	FILE *fp = fopen(path, "r");
	if (!fp) {
		fprintf(stderr, "swl_config_load: cannot open '%s': %s\n",
		        path, strerror(errno));
		return -1;
	}

	char errbuf[256];
	toml_table_t *root = toml_parse_file(fp, errbuf, sizeof(errbuf));
	fclose(fp);

	if (!root) {
		fprintf(stderr, "swl_config_load: parse error in '%s': %s\n",
		        path, errbuf);
		return -1;
	}

	/* Reset file-scope state for commands */
	named_cmds = nullptr;
	named_cmds_count = 0;
	modkey_value = WLR_MODIFIER_ALT;

	/* [general] */
	toml_table_t *general = toml_table_in(root, "general");
	if (general)
		parse_general(general, config);

	/* [appearance] */
	toml_table_t *appearance = toml_table_in(root, "appearance");
	if (appearance)
		parse_appearance(appearance, config);

	/* [keyboard] */
	toml_table_t *keyboard = toml_table_in(root, "keyboard");
	if (keyboard)
		parse_keyboard(keyboard, config);

	/* [trackpad] */
	toml_table_t *trackpad = toml_table_in(root, "trackpad");
	if (trackpad)
		parse_trackpad(trackpad, config);

	/* [scroller] */
	toml_table_t *scroller = toml_table_in(root, "scroller");
	if (scroller)
		parse_scroller(scroller, config);

	/* [commands] -- must be parsed before keybinds */
	toml_table_t *commands = toml_table_in(root, "commands");
	if (commands)
		parse_commands(commands);

	/* [[layout]] -- must be parsed before monitors and keybinds */
	toml_array_t *layout_arr = toml_array_in(root, "layout");
	if (layout_arr)
		parse_layouts(layout_arr, config);

	/* [[rule]] */
	toml_array_t *rule_arr = toml_array_in(root, "rule");
	if (rule_arr)
		parse_rules(rule_arr, config);

	/* [[monitor]] -- parsed after layouts */
	toml_array_t *monitor_arr = toml_array_in(root, "monitor");
	if (monitor_arr)
		parse_monitors(monitor_arr, config);

	/* [bind] -- key and mouse bindings */
	toml_table_t *bind = toml_table_in(root, "bind");
	if (bind) {
		toml_array_t *key_arr = toml_array_in(bind, "key");
		if (key_arr)
			parse_key_bindings(key_arr, config);

		toml_array_t *mouse_arr = toml_array_in(bind, "mouse");
		if (mouse_arr)
			parse_mouse_bindings(mouse_arr, config);
	}

	toml_free(root);
	return 0;
}
