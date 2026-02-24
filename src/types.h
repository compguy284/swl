#ifndef SWL_TYPES_H
#define SWL_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_keyboard_group.h>
#include <wlr/types/wlr_output.h>
#include <scenefx/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_pointer_constraints_v1.h>
#include <wlr/types/wlr_session_lock_v1.h>
#include <xkbcommon/xkbcommon.h>
#ifdef XWAYLAND
#include <wlr/xwayland.h>
#include <xcb/xcb_icccm.h>
#endif

/* enums */
enum SwlCursorMode : unsigned int { CurNormal, CurPressed, CurMove, CurResize }; /* cursor */
enum SwlClientType : unsigned int { XDGShell, LayerShell, X11 }; /* client types */
enum SwlLayer : unsigned int { LyrBg, LyrBottom, LyrTile, LyrFloat, LyrTop, LyrFS, LyrOverlay, LyrBlock, NUM_LAYERS }; /* scene layers */
enum SwlScrollerCenter : unsigned int { ScrollCenterNever, ScrollCenterAlways, ScrollCenterOverflow };

/* forward declarations */
typedef struct SwlServer SwlServer;
typedef struct Monitor Monitor;

typedef union {
	int i;
	uint32_t ui;
	float f;
	const void *v;
} Arg;

typedef struct {
	unsigned int mod;
	unsigned int button;
	void (*func)(SwlServer *, const Arg *);
	Arg arg;
} Button;

typedef struct {
	/* Must keep this field first */
	unsigned int type; /* XDGShell or X11 */

	SwlServer *server;
	Monitor *mon;
	struct wlr_scene_tree *scene;
	struct wlr_scene_rect *border;    /* single background rect for border */
	struct wlr_scene_shadow *shadow;  /* scenefx shadow node */
	struct wlr_scene_tree *scene_surface;
	struct wl_list link;
	struct wl_list flink;
	struct wlr_box geom; /* layout-relative, includes border */
	struct wlr_box prev; /* layout-relative, includes border */
	struct wlr_box bounds; /* only width and height are used */
	union {
		struct wlr_xdg_surface *xdg;
#ifdef XWAYLAND
		struct wlr_xwayland_surface *xwayland;
#endif
	} surface;
	struct wlr_xdg_toplevel_decoration_v1 *decoration;
	struct wl_listener commit;
	struct wl_listener map;
	struct wl_listener maximize;
	struct wl_listener unmap;
	struct wl_listener destroy;
	struct wl_listener set_title;
	struct wl_listener fullscreen;
	struct wl_listener set_decoration_mode;
	struct wl_listener destroy_decoration;
#ifdef XWAYLAND
	struct wl_listener activate;
	struct wl_listener associate;
	struct wl_listener dissociate;
	struct wl_listener configure;
	struct wl_listener set_hints;
#endif
	unsigned int bw;
	uint32_t tags;
	bool isfloating, isurgent, isfullscreen;
	uint32_t resize; /* configure serial of a pending resize */
	float scroller_cw;         /* column width: fraction (0<x<=1) or pixels (>1), 0 = use default */
	int scroller_preset_idx;   /* index into preset_column_widths for cycling */
} Client;

typedef struct {
	uint32_t mod;
	xkb_keysym_t keysym;
	void (*func)(SwlServer *, const Arg *);
	Arg arg;
} Key;

typedef struct {
	SwlServer *server;
	struct wlr_keyboard_group *wlr_group;

	int nsyms;
	const xkb_keysym_t *keysyms; /* invalid if nsyms == 0 */
	uint32_t mods; /* invalid if nsyms == 0 */
	struct wl_event_source *key_repeat_source;

	struct wl_listener modifiers;
	struct wl_listener key;
	struct wl_listener destroy;
} KeyboardGroup;

typedef struct {
	/* Must keep this field first */
	unsigned int type; /* LayerShell */

	SwlServer *server;
	Monitor *mon;
	struct wlr_scene_tree *scene;
	struct wlr_scene_tree *popups;
	struct wlr_scene_layer_surface_v1 *scene_layer;
	struct wl_list link;
	bool mapped;
	struct wlr_layer_surface_v1 *layer_surface;

	struct wl_listener destroy;
	struct wl_listener unmap;
	struct wl_listener surface_commit;
} LayerSurface;

static_assert(offsetof(Client, type) == 0, "Client.type must be first field");
static_assert(offsetof(LayerSurface, type) == 0, "LayerSurface.type must be first field");

typedef struct {
	const char *symbol;
	void (*arrange)(SwlServer *, Monitor *);
} Layout;

struct Monitor {
	SwlServer *server;
	struct wl_list link;
	struct wlr_output *wlr_output;
	struct wlr_scene_output *scene_output;
	struct wlr_scene_rect *fullscreen_bg; /* See swl_handle_new_output() for info */
	struct wl_listener frame;
	struct wl_listener destroy;
	struct wl_listener request_state;
	struct wl_listener destroy_lock_surface;
	struct wlr_session_lock_surface_v1 *lock_surface;
	struct wlr_box m; /* monitor area, layout-relative */
	struct wlr_box w; /* window area, layout-relative */
	struct wl_list layers[4]; /* LayerSurface.link */
	const Layout *lt[2];
	unsigned int seltags;
	unsigned int sellt;
	uint32_t tagset[2];
	float mfact;
	bool gamma_lut_changed;
	int nmaster;
	int scroll_x;              /* horizontal viewport offset for scroller layout */
	char ltsymbol[16];
	bool asleep;
};

typedef struct {
	const char *name;
	float mfact;
	int nmaster;
	float scale;
	const Layout *lt;
	enum wl_output_transform rr;
	int x, y;
} MonitorRule;

typedef struct {
	SwlServer *server;
	struct wlr_pointer_constraint_v1 *constraint;
	struct wl_listener destroy;
} PointerConstraint;

typedef struct {
	const char *id;
	const char *title;
	uint32_t tags;
	int isfloating;
	int monitor;
} Rule;

typedef struct {
	SwlServer *server;
	struct wlr_scene_tree *scene;

	struct wlr_session_lock_v1 *lock;
	struct wl_listener new_surface;
	struct wl_listener unlock;
	struct wl_listener destroy;
} SessionLock;

#endif
