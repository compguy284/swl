#include <stdlib.h>
#include <time.h>
#include <scenefx/types/wlr_scene.h>
#include <wlr/types/wlr_output.h>

#include "animation.h"
#include "config.h"
#include "server.h"

SwlBakedCurves swl_curves;

uint32_t
swl_now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

void
swl_bezier_bake(const double cp[4], SwlVec2 *out, int count)
{
	/* cp = {x1, y1, x2, y2} — control points of a cubic bezier
	 * with implicit P0=(0,0) and P3=(1,1). */
	for (int i = 0; i < count; i++) {
		double t = (double)i / (count - 1);
		double mt = 1.0 - t;
		/* B(t) = 3*mt^2*t*P1 + 3*mt*t^2*P2 + t^3 */
		out[i].x = 3.0 * mt * mt * t * cp[0]
			+ 3.0 * mt * t * t * cp[2]
			+ t * t * t;
		out[i].y = 3.0 * mt * mt * t * cp[1]
			+ 3.0 * mt * t * t * cp[3]
			+ t * t * t;
	}
}

double
swl_bezier_lookup(const SwlVec2 *points, int count, double t)
{
	if (t <= 0.0) return 0.0;
	if (t >= 1.0) return 1.0;

	int down = 0;
	int up = count - 1;
	while (up - down > 1) {
		int mid = (up + down) / 2;
		if (points[mid].x <= t)
			down = mid;
		else
			up = mid;
	}
	return points[up].y;
}

void
swl_animation_init_curves(const SwlConfig *cfg)
{
	swl_bezier_bake(cfg->anim_curve_open, swl_curves.open, SWL_BEZIER_POINTS);
	swl_bezier_bake(cfg->anim_curve_move, swl_curves.move, SWL_BEZIER_POINTS);
	swl_bezier_bake(cfg->anim_curve_close, swl_curves.close, SWL_BEZIER_POINTS);
}

void
swl_animation_start(SwlAnimation *anim, enum SwlAnimAction action,
	uint32_t duration, const struct wlr_box *initial, const struct wlr_box *target)
{
	anim->running = true;
	anim->action = action;
	anim->time_started = swl_now_ms();
	anim->duration = duration;
	anim->initial = *initial;
	anim->target = *target;
	anim->current = *initial;
}

double
swl_animation_tick(SwlAnimation *anim)
{
	if (!anim->running)
		return -1.0;

	uint32_t now = swl_now_ms();
	uint32_t elapsed = now - anim->time_started;
	double t = (double)elapsed / (double)anim->duration;

	if (t >= 1.0) {
		anim->running = false;
		anim->action = AnimNone;
		anim->current = anim->target;
		return 1.0;
	}

	/* Look up easing factor from the appropriate baked curve */
	const SwlVec2 *curve;
	switch (anim->action) {
	case AnimOpen:  curve = swl_curves.open;  break;
	case AnimClose: curve = swl_curves.close; break;
	case AnimMove:  curve = swl_curves.move;  break;
	default:        curve = swl_curves.move;  break;
	}

	double factor = swl_bezier_lookup(curve, SWL_BEZIER_POINTS, t);

	/* Interpolate geometry */
	anim->current.x = anim->initial.x
		+ (int)((anim->target.x - anim->initial.x) * factor);
	anim->current.y = anim->initial.y
		+ (int)((anim->target.y - anim->initial.y) * factor);
	anim->current.width = anim->initial.width
		+ (int)((anim->target.width - anim->initial.width) * factor);
	anim->current.height = anim->initial.height
		+ (int)((anim->target.height - anim->initial.height) * factor);

	return factor;
}

/* ===== Scene snapshot for close animations ===== */

static bool
scene_node_snapshot(struct wlr_scene_node *node, int32_t lx, int32_t ly,
	struct wlr_scene_tree *snapshot_tree)
{
	if (!node->enabled && node->type != WLR_SCENE_NODE_TREE)
		return true;

	lx += node->x;
	ly += node->y;

	struct wlr_scene_node *snapshot_node = NULL;
	switch (node->type) {
	case WLR_SCENE_NODE_TREE: {
		struct wlr_scene_tree *scene_tree = wlr_scene_tree_from_node(node);
		struct wlr_scene_node *child;
		wl_list_for_each(child, &scene_tree->children, link) {
			scene_node_snapshot(child, lx, ly, snapshot_tree);
		}
		break;
	}
	case WLR_SCENE_NODE_RECT:
		/* Skip rects (borders) — they complicate the snapshot
		 * and the fadeout looks cleaner without them. */
		break;
	case WLR_SCENE_NODE_BUFFER: {
		struct wlr_scene_buffer *scene_buffer =
			wlr_scene_buffer_from_node(node);

		struct wlr_scene_buffer *snapshot_buffer =
			wlr_scene_buffer_create(snapshot_tree, NULL);
		if (!snapshot_buffer)
			return false;
		snapshot_node = &snapshot_buffer->node;

		wlr_scene_buffer_set_dest_size(snapshot_buffer,
			scene_buffer->dst_width, scene_buffer->dst_height);
		wlr_scene_buffer_set_opaque_region(snapshot_buffer,
			&scene_buffer->opaque_region);
		wlr_scene_buffer_set_source_box(snapshot_buffer,
			&scene_buffer->src_box);
		wlr_scene_buffer_set_transform(snapshot_buffer,
			scene_buffer->transform);
		wlr_scene_buffer_set_filter_mode(snapshot_buffer,
			scene_buffer->filter_mode);
		wlr_scene_buffer_set_opacity(snapshot_buffer,
			scene_buffer->opacity);
		wlr_scene_buffer_set_corner_radius(snapshot_buffer,
			scene_buffer->corners.top_left);

		struct wlr_scene_surface *scene_surface =
			wlr_scene_surface_try_from_buffer(scene_buffer);
		if (scene_surface && scene_surface->surface->buffer)
			wlr_scene_buffer_set_buffer(snapshot_buffer,
				&scene_surface->surface->buffer->base);
		else
			wlr_scene_buffer_set_buffer(snapshot_buffer,
				scene_buffer->buffer);
		break;
	}
	case WLR_SCENE_NODE_SHADOW: {
		struct wlr_scene_shadow *scene_shadow =
			wlr_scene_shadow_from_node(node);

		struct wlr_scene_shadow *snapshot_shadow = wlr_scene_shadow_create(
			snapshot_tree, scene_shadow->width, scene_shadow->height,
			scene_shadow->corner_radius, scene_shadow->blur_sigma,
			scene_shadow->color);
		if (!snapshot_shadow)
			return false;
		snapshot_node = &snapshot_shadow->node;
		wlr_scene_shadow_set_clipped_region(snapshot_shadow,
			scene_shadow->clipped_region);
		wlr_scene_node_set_enabled(&snapshot_shadow->node, false);
		break;
	}
	case WLR_SCENE_NODE_BLUR:
	case WLR_SCENE_NODE_OPTIMIZED_BLUR:
		return true;
	}

	if (snapshot_node)
		wlr_scene_node_set_position(snapshot_node, lx, ly);

	return true;
}

struct wlr_scene_tree *
swl_scene_tree_snapshot(struct wlr_scene_node *node,
	struct wlr_scene_tree *parent)
{
	struct wlr_scene_tree *snapshot = wlr_scene_tree_create(parent);
	if (!snapshot)
		return NULL;

	wlr_scene_node_set_enabled(&snapshot->node, false);

	if (!scene_node_snapshot(node, 0, 0, snapshot)) {
		wlr_scene_node_destroy(&snapshot->node);
		return NULL;
	}

	wlr_scene_node_set_enabled(&snapshot->node, true);
	return snapshot;
}

void
swl_request_frame_all(SwlServer *server)
{
	Monitor *m;
	wl_list_for_each(m, &server->mons, link) {
		if (m->wlr_output->enabled)
			wlr_output_schedule_frame(m->wlr_output);
	}
}
