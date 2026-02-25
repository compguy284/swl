#ifndef SWL_MACROS_H
#define SWL_MACROS_H

#include "util.h"

#define MAX(A, B) ((A) > (B) ? (A) : (B))
#define MIN(A, B) ((A) < (B) ? (A) : (B))
#define CLEANMASK(mask)         (mask & ~WLR_MODIFIER_CAPS)
#define VISIBLEON(C, M)         ((M) && (C)->mon == (M))
#define LENGTH(X)               (sizeof X / sizeof X[0])
#define END(A)                  ((A) + LENGTH(A))
#define LISTEN(E, L, H)         wl_signal_add((E), ((L)->notify = (H), (L)))

/* Wrapper for heap-allocated listeners that carry a SwlServer back-pointer */
typedef struct {
	struct wl_listener listener;
	SwlServer *server;
} SwlListener;

#define LISTEN_STATIC(E, H, S) do { \
	SwlListener *_sl = ecalloc(1, sizeof(*_sl)); \
	_sl->server = (S); \
	_sl->listener.notify = (H); \
	wl_signal_add((E), &_sl->listener); \
} while (0)

#endif
