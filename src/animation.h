#ifndef SWL_ANIMATION_H
#define SWL_ANIMATION_H

#include <stdbool.h>
#include <stdint.h>
#include <wayland-server-core.h>
#include <wlr/util/box.h>

/* Forward declarations */
typedef struct SwlConfig SwlConfig;
typedef struct SwlServer SwlServer;
struct wlr_scene_tree;
struct wlr_scene_node;

/* Baked bezier curve resolution */
#define SWL_BEZIER_POINTS 1024

typedef struct { double x, y; } SwlVec2;

enum SwlAnimAction { AnimNone, AnimOpen, AnimMove, AnimClose };

typedef struct {
	bool running;
	enum SwlAnimAction action;
	uint32_t time_started;   /* CLOCK_MONOTONIC ms */
	uint32_t duration;       /* ms */
	struct wlr_box initial;  /* start geometry */
	struct wlr_box target;   /* end geometry */
	struct wlr_box current;  /* interpolated current */
} SwlAnimation;

/* Fadeout client — animating snapshot of a closed window */
typedef struct {
	struct wl_list link;     /* SwlServer.fadeout_clients */
	struct wlr_scene_tree *snapshot;
	SwlAnimation animation;
	float start_opacity;
	bool fade_opacity;       /* true for fade/zoom, false for slide */
} SwlFadeout;

/* Pre-baked curves for each action type */
typedef struct {
	SwlVec2 open[SWL_BEZIER_POINTS];
	SwlVec2 move[SWL_BEZIER_POINTS];
	SwlVec2 close[SWL_BEZIER_POINTS];
} SwlBakedCurves;

/* Global baked curves — initialized by swl_animation_init_curves() */
extern SwlBakedCurves swl_curves;

/* Bake a cubic bezier defined by control points cp[4] = {x1,y1,x2,y2}
 * into `count` pre-computed points. */
void swl_bezier_bake(const double cp[4], SwlVec2 *out, int count);

/* Binary-search lookup: given progress t (0-1 on x-axis),
 * return the easing factor (y value). */
double swl_bezier_lookup(const SwlVec2 *points, int count, double t);

/* Bake all configured curves. Call after config load/reload. */
void swl_animation_init_curves(const SwlConfig *cfg);

/* Tick an animation forward. Returns easing factor (0-1), or -1 if not running.
 * Updates anim->current. Marks running=false when complete. */
double swl_animation_tick(SwlAnimation *anim);

/* Start an animation. */
void swl_animation_start(SwlAnimation *anim, enum SwlAnimAction action,
	uint32_t duration, const struct wlr_box *initial, const struct wlr_box *target);

/* CLOCK_MONOTONIC milliseconds helper */
uint32_t swl_now_ms(void);

/* Create a scene-graph snapshot of a node's subtree (for close animation). */
struct wlr_scene_tree *swl_scene_tree_snapshot(struct wlr_scene_node *node,
	struct wlr_scene_tree *parent);

/* Schedule a frame on all enabled monitors (to keep animations ticking). */
void swl_request_frame_all(SwlServer *server);

#endif
