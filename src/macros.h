#ifndef SWL_MACROS_H
#define SWL_MACROS_H

#include "util.h"

#define MAX(A, B) ({ typeof(A) _a = (A); typeof(B) _b = (B); _a > _b ? _a : _b; })
#define MIN(A, B) ({ typeof(A) _a = (A); typeof(B) _b = (B); _a < _b ? _a : _b; })
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
