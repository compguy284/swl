#ifndef SWL_IPC_H
#define SWL_IPC_H

#include <stddef.h>
#include <stdint.h>
#include <wayland-server-core.h>
#include "types.h"

/* Event type bitmask for subscriptions */
enum swl_ipc_event {
	SWL_IPC_EVENT_FOCUS        = 1 << 0,
	SWL_IPC_EVENT_WINDOW_OPEN  = 1 << 1,
	SWL_IPC_EVENT_WINDOW_CLOSE = 1 << 2,
	SWL_IPC_EVENT_LAYOUT       = 1 << 3,
	SWL_IPC_EVENT_FULLSCREEN   = 1 << 4,
	SWL_IPC_EVENT_FLOATING     = 1 << 5,
	SWL_IPC_EVENT_TITLE        = 1 << 6,
	SWL_IPC_EVENT_MONITOR      = 1 << 7,
	SWL_IPC_EVENT_CONFIG       = 1 << 8,
};

/* Per-connection state */
typedef struct swl_ipc_client {
	struct wl_list link;
	struct swl_ipc *ipc;
	struct wl_event_source *source;
	int fd;
	uint32_t subscriptions;        /* bitmask of swl_ipc_event */
	char buf[4096];                /* read buffer */
	size_t buf_len;
	char wbuf[65536];             /* write buffer */
	size_t wbuf_len;
	size_t wbuf_off;
} SwlIpcClient;

/* Main IPC state */
typedef struct swl_ipc {
	SwlServer *server;
	int listen_fd;
	struct wl_event_source *listen_source;
	struct wl_list clients;        /* SwlIpcClient.link */
	char sock_path[256];
} SwlIpc;

/* Lifecycle */
SwlIpc *swl_ipc_init(SwlServer *server);
void swl_ipc_cleanup(SwlIpc *ipc);

/* Event notifications — all are no-ops when ipc is NULL */
void swl_ipc_notify_focus(SwlIpc *ipc, Client *c);
void swl_ipc_notify_window_open(SwlIpc *ipc, Client *c);
void swl_ipc_notify_window_close(SwlIpc *ipc, Client *c);
void swl_ipc_notify_layout(SwlIpc *ipc, Monitor *m);
void swl_ipc_notify_fullscreen(SwlIpc *ipc, Client *c);
void swl_ipc_notify_floating(SwlIpc *ipc, Client *c);
void swl_ipc_notify_title(SwlIpc *ipc, Client *c);
void swl_ipc_notify_monitor(SwlIpc *ipc, Monitor *m, int added);
void swl_ipc_notify_config_reload(SwlIpc *ipc);

#endif
