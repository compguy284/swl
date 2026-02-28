#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/util/log.h>
#include <xkbcommon/xkbcommon.h>

#include "ipc.h"
#include "client.h"
#include "commands.h"
#include "config.h"
#include "server.h"

/* =====================================================================
 * Helpers
 * ===================================================================== */

static void
ipc_client_destroy(SwlIpcClient *ic)
{
	wl_event_source_remove(ic->source);
	close(ic->fd);
	wl_list_remove(&ic->link);
	free(ic);
}

static int
ipc_client_write(SwlIpcClient *ic, const char *data, size_t len)
{
	/* Try direct write first if no buffered data */
	if (ic->wbuf_len == 0) {
		ssize_t n = write(ic->fd, data, len);
		if (n < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				n = 0;
			else
				return -1;
		}
		if ((size_t)n == len)
			return 0;
		data += n;
		len -= (size_t)n;
	}

	/* Buffer the rest */
	size_t avail = sizeof(ic->wbuf) - ic->wbuf_len;
	if (len > avail)
		return -1; /* write buffer full, disconnect client */

	memcpy(ic->wbuf + ic->wbuf_len, data, len);
	ic->wbuf_len += len;

	/* Enable writable notification */
	wl_event_source_fd_update(ic->source,
		WL_EVENT_READABLE | WL_EVENT_WRITABLE);
	return 0;
}

static int
ipc_client_sendf(SwlIpcClient *ic, const char *fmt, ...)
	__attribute__((format(printf, 2, 3)));

static int
ipc_client_sendf(SwlIpcClient *ic, const char *fmt, ...)
{
	char buf[4096];
	va_list ap;
	va_start(ap, fmt);
	int len = vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	if (len < 0 || (size_t)len >= sizeof(buf))
		return -1;
	return ipc_client_write(ic, buf, (size_t)len);
}

static void
format_client(char *buf, size_t sz, Client *c, Monitor *m)
{
	const char *title = swl_client_get_title(c);
	const char *appid = swl_client_get_appid(c);
	const char *monname = (m && m->wlr_output) ? m->wlr_output->name : "";
	snprintf(buf, sz,
		"title=\"%s\" appid=\"%s\" mon=\"%s\" floating=%d fullscreen=%d x=%d y=%d w=%d h=%d",
		title ? title : "",
		appid ? appid : "",
		monname,
		c->isfloating, c->isfullscreen,
		c->geom.x, c->geom.y, c->geom.width, c->geom.height);
}

/* =====================================================================
 * Arg parsing for IPC cmd
 * ===================================================================== */

enum arg_type { ARG_NONE, ARG_INT, ARG_FLOAT, ARG_DIR, ARG_SPAWN };

static const struct {
	const char *name;
	enum arg_type atype;
} cmd_arg_types[] = {
	{ "spawn",               ARG_SPAWN },
	{ "killclient",          ARG_NONE },
	{ "focusstack",          ARG_INT },
	{ "focusmon",            ARG_DIR },
	{ "togglefloating",      ARG_NONE },
	{ "togglefullscreen",    ARG_NONE },
	{ "tagmon",              ARG_DIR },
	{ "moveresize",          ARG_NONE },
	{ "quit",                ARG_NONE },
	{ "chvt",                ARG_INT },
	{ "scroller_cycle_width", ARG_INT },
	{ "scroller_set_width",  ARG_FLOAT },
	{ "consume_or_expel",    ARG_INT },
	{ "focusdir",            ARG_INT },
	{ "swapdir",             ARG_INT },
	{ "reload_config",       ARG_NONE },
};

static enum arg_type
get_arg_type(const char *action)
{
	for (size_t i = 0; i < sizeof(cmd_arg_types) / sizeof(cmd_arg_types[0]); i++) {
		if (strcmp(cmd_arg_types[i].name, action) == 0)
			return cmd_arg_types[i].atype;
	}
	return ARG_NONE;
}

/* =====================================================================
 * Request handling
 * ===================================================================== */

static void
handle_cmd(SwlIpc *ipc, SwlIpcClient *ic, char *args)
{
	if (!args || !*args) {
		ipc_client_sendf(ic, "error missing action\n");
		return;
	}

	/* Split "action [arg]" */
	char *action = args;
	char *arg_str = strchr(args, ' ');
	if (arg_str) {
		*arg_str = '\0';
		arg_str++;
		while (*arg_str == ' ') arg_str++;
		if (!*arg_str) arg_str = nullptr;
	}

	SwlCmdFunc func = swl_find_action(action);
	if (!func) {
		ipc_client_sendf(ic, "error unknown action '%s'\n", action);
		return;
	}

	Arg arg = {0};
	enum arg_type atype = get_arg_type(action);

	switch (atype) {
	case ARG_INT:
		if (arg_str)
			arg.i = atoi(arg_str);
		break;
	case ARG_FLOAT:
		if (arg_str)
			arg.f = (float)atof(arg_str);
		break;
	case ARG_DIR:
		if (arg_str) {
			if (strcmp(arg_str, "left") == 0)
				arg.i = WLR_DIRECTION_LEFT;
			else if (strcmp(arg_str, "right") == 0)
				arg.i = WLR_DIRECTION_RIGHT;
		}
		break;
	case ARG_SPAWN:
		/* For spawn via IPC, we don't support named commands — would
		 * need the static named_cmds from config.c. Just pass nullptr. */
		if (arg_str)
			wlr_log(WLR_INFO, "ipc: spawn with arg '%s' not supported, use keybindings", arg_str);
		break;
	case ARG_NONE:
		break;
	}

	func(ipc->server, &arg);
	ipc_client_sendf(ic, "ok\n");
}

static void
handle_query(SwlIpc *ipc, SwlIpcClient *ic, char *what)
{
	if (!what || !*what) {
		ipc_client_sendf(ic, "error missing query target\n");
		return;
	}

	SwlServer *server = ipc->server;

	if (strcmp(what, "clients") == 0) {
		Client *c;
		wl_list_for_each(c, &server->clients, link) {
			char info[1024];
			format_client(info, sizeof(info), c, c->mon);
			if (ipc_client_sendf(ic, "client %s\n", info) < 0)
				return;
		}
		ipc_client_sendf(ic, "end\n");
	} else if (strcmp(what, "monitors") == 0) {
		Monitor *m;
		wl_list_for_each(m, &server->mons, link) {
			if (ipc_client_sendf(ic, "monitor name=\"%s\" w=%d h=%d scale=%.1f selmon=%d\n",
				m->wlr_output->name,
				m->m.width, m->m.height,
				m->wlr_output->scale,
				m == server->selmon ? 1 : 0) < 0)
				return;
		}
		ipc_client_sendf(ic, "end\n");
	} else if (strcmp(what, "keybinds") == 0) {
		const SwlConfig *cfg = &server->config;
		for (size_t i = 0; i < cfg->keys_count; i++) {
			const Key *k = &cfg->keys[i];
			const char *action = swl_find_action_name(k->func);
			if (!action)
				continue;

			/* Format modifier string */
			char mods[128] = "";
			size_t moff = 0;
			static const struct { uint32_t mod; const char *name; } modtab[] = {
				{ WLR_MODIFIER_SHIFT, "shift" },
				{ WLR_MODIFIER_CTRL,  "ctrl" },
				{ WLR_MODIFIER_ALT,   "alt" },
				{ WLR_MODIFIER_LOGO,  "super" },
			};
			for (size_t j = 0; j < sizeof(modtab) / sizeof(modtab[0]); j++) {
				if (k->mod & modtab[j].mod) {
					if (moff > 0)
						mods[moff++] = '+';
					size_t nlen = strlen(modtab[j].name);
					if (moff + nlen < sizeof(mods)) {
						memcpy(mods + moff, modtab[j].name, nlen);
						moff += nlen;
					}
				}
			}
			mods[moff] = '\0';

			/* Format key name */
			char keyname[64];
			xkb_keysym_get_name(k->keysym, keyname, sizeof(keyname));

			/* Format arg if present */
			char argstr[128] = "";
			enum arg_type atype = get_arg_type(action);
			switch (atype) {
			case ARG_INT:
				snprintf(argstr, sizeof(argstr), " arg=%d", k->arg.i);
				break;
			case ARG_FLOAT:
				snprintf(argstr, sizeof(argstr), " arg=%.2f", (double)k->arg.f);
				break;
			case ARG_DIR:
				snprintf(argstr, sizeof(argstr), " arg=\"%s\"",
					k->arg.i == WLR_DIRECTION_LEFT ? "left" :
					k->arg.i == WLR_DIRECTION_RIGHT ? "right" : "unknown");
				break;
			case ARG_SPAWN:
				if (k->arg.v) {
					const char **argv = k->arg.v;
					size_t aoff = 0;
					aoff += (size_t)snprintf(argstr + aoff, sizeof(argstr) - aoff, " arg=\"");
					for (const char **p = argv; *p && aoff < sizeof(argstr) - 2; p++) {
						if (p != argv)
							aoff += (size_t)snprintf(argstr + aoff, sizeof(argstr) - aoff, " ");
						aoff += (size_t)snprintf(argstr + aoff, sizeof(argstr) - aoff, "%s", *p);
					}
					snprintf(argstr + aoff, sizeof(argstr) - aoff, "\"");
				}
				break;
			case ARG_NONE:
				break;
			}

			if (ipc_client_sendf(ic, "keybind mods=\"%s\" key=\"%s\" action=\"%s\"%s\n",
				mods, keyname, action, argstr) < 0)
				return;
		}
		ipc_client_sendf(ic, "end\n");
	} else if (strcmp(what, "focused") == 0) {
		Client *c = swl_focustop(server, server->selmon);
		if (c) {
			char info[1024];
			format_client(info, sizeof(info), c, c->mon);
			ipc_client_sendf(ic, "client %s\n", info);
		}
		ipc_client_sendf(ic, "end\n");
	} else {
		ipc_client_sendf(ic, "error unknown query '%s'\n", what);
	}
}

static void
handle_subscribe(SwlIpcClient *ic, char *events)
{
	if (!events || !*events) {
		ipc_client_sendf(ic, "error missing event list\n");
		return;
	}

	static const struct {
		const char *name;
		uint32_t bit;
	} event_map[] = {
		{ "focus",         SWL_IPC_EVENT_FOCUS },
		{ "window_open",   SWL_IPC_EVENT_WINDOW_OPEN },
		{ "window_close",  SWL_IPC_EVENT_WINDOW_CLOSE },
		{ "layout",        SWL_IPC_EVENT_LAYOUT },
		{ "fullscreen",    SWL_IPC_EVENT_FULLSCREEN },
		{ "floating",      SWL_IPC_EVENT_FLOATING },
		{ "title",         SWL_IPC_EVENT_TITLE },
		{ "monitor",       SWL_IPC_EVENT_MONITOR },
		{ "config_reload", SWL_IPC_EVENT_CONFIG },
	};

	/* Parse comma-separated event names */
	char *tok = strtok(events, ",");
	while (tok) {
		while (*tok == ' ') tok++;
		bool found = false;
		for (size_t i = 0; i < sizeof(event_map) / sizeof(event_map[0]); i++) {
			if (strcmp(tok, event_map[i].name) == 0) {
				ic->subscriptions |= event_map[i].bit;
				found = true;
				break;
			}
		}
		if (!found) {
			ipc_client_sendf(ic, "error unknown event '%s'\n", tok);
			return;
		}
		tok = strtok(nullptr, ",");
	}

	ipc_client_sendf(ic, "ok\n");
}

static void
handle_request(SwlIpc *ipc, SwlIpcClient *ic, char *line)
{
	/* Strip trailing whitespace */
	size_t len = strlen(line);
	while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r' || line[len - 1] == ' '))
		line[--len] = '\0';

	if (len == 0)
		return;

	/* Split verb and rest */
	char *rest = strchr(line, ' ');
	if (rest) {
		*rest = '\0';
		rest++;
		while (*rest == ' ') rest++;
		if (!*rest) rest = nullptr;
	}

	if (strcmp(line, "cmd") == 0)
		handle_cmd(ipc, ic, rest);
	else if (strcmp(line, "query") == 0)
		handle_query(ipc, ic, rest);
	else if (strcmp(line, "subscribe") == 0)
		handle_subscribe(ic, rest);
	else
		ipc_client_sendf(ic, "error unknown verb '%s'\n", line);
}

/* =====================================================================
 * Event loop callbacks
 * ===================================================================== */

static int
ipc_client_cb(int fd, uint32_t mask, void *data)
{
	SwlIpcClient *ic = data;

	if (mask & WL_EVENT_ERROR || mask & WL_EVENT_HANGUP) {
		ipc_client_destroy(ic);
		return 0;
	}

	if (mask & WL_EVENT_WRITABLE) {
		size_t pending = ic->wbuf_len - ic->wbuf_off;
		if (pending > 0) {
			ssize_t n = write(fd, ic->wbuf + ic->wbuf_off, pending);
			if (n < 0) {
				if (errno != EAGAIN && errno != EWOULDBLOCK) {
					ipc_client_destroy(ic);
					return 0;
				}
			} else {
				ic->wbuf_off += (size_t)n;
				if (ic->wbuf_off == ic->wbuf_len) {
					ic->wbuf_off = 0;
					ic->wbuf_len = 0;
					wl_event_source_fd_update(ic->source, WL_EVENT_READABLE);
				}
			}
		}
	}

	if (mask & WL_EVENT_READABLE) {
		size_t avail = sizeof(ic->buf) - ic->buf_len;
		if (avail == 0) {
			/* Buffer full with no newline — protocol error */
			ipc_client_sendf(ic, "error request too long\n");
			ic->buf_len = 0;
			return 0;
		}

		ssize_t n = read(fd, ic->buf + ic->buf_len, avail);
		if (n <= 0) {
			ipc_client_destroy(ic);
			return 0;
		}
		ic->buf_len += (size_t)n;

		/* Process complete lines */
		char *start = ic->buf;
		char *nl;
		while ((nl = memchr(start, '\n', ic->buf_len - (size_t)(start - ic->buf))) != nullptr) {
			*nl = '\0';
			handle_request(ic->ipc, ic, start);
			start = nl + 1;
		}

		/* Move remaining partial line to front */
		size_t remaining = ic->buf_len - (size_t)(start - ic->buf);
		if (remaining > 0 && start != ic->buf)
			memmove(ic->buf, start, remaining);
		ic->buf_len = remaining;
	}

	return 0;
}

static int
ipc_accept_cb(int fd, uint32_t mask, void *data)
{
	SwlIpc *ipc = data;
	(void)mask;

	int client_fd = accept(fd, nullptr, nullptr);
	if (client_fd < 0) {
		wlr_log(WLR_ERROR, "ipc: accept failed: %s", strerror(errno));
		return 0;
	}
	fcntl(client_fd, F_SETFD, FD_CLOEXEC);
	fcntl(client_fd, F_SETFL, fcntl(client_fd, F_GETFL) | O_NONBLOCK);

	SwlIpcClient *ic = calloc(1, sizeof(*ic));
	if (!ic) {
		close(client_fd);
		return 0;
	}

	ic->fd = client_fd;
	ic->ipc = ipc;
	struct wl_event_loop *loop = wl_display_get_event_loop(ipc->server->dpy);
	ic->source = wl_event_loop_add_fd(loop, client_fd,
		WL_EVENT_READABLE, ipc_client_cb, ic);
	if (!ic->source) {
		close(client_fd);
		free(ic);
		return 0;
	}

	wl_list_insert(&ipc->clients, &ic->link);
	return 0;
}

/* =====================================================================
 * Lifecycle
 * ===================================================================== */

SwlIpc *
swl_ipc_init(SwlServer *server)
{
	const char *runtime_dir = getenv("XDG_RUNTIME_DIR");
	const char *wayland_display = getenv("WAYLAND_DISPLAY");
	if (!runtime_dir || !wayland_display) {
		wlr_log(WLR_ERROR, "ipc: XDG_RUNTIME_DIR or WAYLAND_DISPLAY not set");
		return nullptr;
	}

	SwlIpc *ipc = calloc(1, sizeof(*ipc));
	if (!ipc)
		return nullptr;

	ipc->server = server;
	wl_list_init(&ipc->clients);

	snprintf(ipc->sock_path, sizeof(ipc->sock_path),
		"%s/swl-%s.sock", runtime_dir, wayland_display);

	/* Remove stale socket */
	unlink(ipc->sock_path);

	ipc->listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (ipc->listen_fd < 0) {
		wlr_log(WLR_ERROR, "ipc: socket() failed: %s", strerror(errno));
		free(ipc);
		return nullptr;
	}
	fcntl(ipc->listen_fd, F_SETFD, FD_CLOEXEC);
	fcntl(ipc->listen_fd, F_SETFL, fcntl(ipc->listen_fd, F_GETFL) | O_NONBLOCK);

	struct sockaddr_un addr = { .sun_family = AF_UNIX };
	if (strlen(ipc->sock_path) >= sizeof(addr.sun_path)) {
		wlr_log(WLR_ERROR, "ipc: socket path too long");
		close(ipc->listen_fd);
		free(ipc);
		return nullptr;
	}
	strncpy(addr.sun_path, ipc->sock_path, sizeof(addr.sun_path) - 1);

	if (bind(ipc->listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		wlr_log(WLR_ERROR, "ipc: bind() failed: %s", strerror(errno));
		close(ipc->listen_fd);
		free(ipc);
		return nullptr;
	}

	if (listen(ipc->listen_fd, 16) < 0) {
		wlr_log(WLR_ERROR, "ipc: listen() failed: %s", strerror(errno));
		unlink(ipc->sock_path);
		close(ipc->listen_fd);
		free(ipc);
		return nullptr;
	}

	struct wl_event_loop *loop = wl_display_get_event_loop(server->dpy);
	ipc->listen_source = wl_event_loop_add_fd(loop, ipc->listen_fd,
		WL_EVENT_READABLE, ipc_accept_cb, ipc);
	if (!ipc->listen_source) {
		wlr_log(WLR_ERROR, "ipc: wl_event_loop_add_fd failed");
		unlink(ipc->sock_path);
		close(ipc->listen_fd);
		free(ipc);
		return nullptr;
	}

	wlr_log(WLR_INFO, "ipc: listening on %s", ipc->sock_path);
	return ipc;
}

void
swl_ipc_cleanup(SwlIpc *ipc)
{
	if (!ipc)
		return;

	SwlIpcClient *ic, *tmp;
	wl_list_for_each_safe(ic, tmp, &ipc->clients, link)
		ipc_client_destroy(ic);

	wl_event_source_remove(ipc->listen_source);
	close(ipc->listen_fd);
	unlink(ipc->sock_path);
	free(ipc);
}

/* =====================================================================
 * Event broadcast
 * ===================================================================== */

static void
ipc_broadcast(SwlIpc *ipc, uint32_t event_bit, const char *msg, size_t len)
{
	SwlIpcClient *ic, *tmp;
	wl_list_for_each_safe(ic, tmp, &ipc->clients, link) {
		if (!(ic->subscriptions & event_bit))
			continue;
		if (ipc_client_write(ic, msg, len) < 0)
			ipc_client_destroy(ic);
	}
}

static void
ipc_broadcastf(SwlIpc *ipc, uint32_t event_bit, const char *fmt, ...)
	__attribute__((format(printf, 3, 4)));

static void
ipc_broadcastf(SwlIpc *ipc, uint32_t event_bit, const char *fmt, ...)
{
	char buf[4096];
	va_list ap;
	va_start(ap, fmt);
	int len = vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	if (len > 0 && (size_t)len < sizeof(buf))
		ipc_broadcast(ipc, event_bit, buf, (size_t)len);
}

/* =====================================================================
 * Event notification functions
 * ===================================================================== */

void
swl_ipc_notify_focus(SwlIpc *ipc, Client *c)
{
	if (!ipc)
		return;
	if (c) {
		char info[1024];
		format_client(info, sizeof(info), c, c->mon);
		ipc_broadcastf(ipc, SWL_IPC_EVENT_FOCUS, "event focus %s\n", info);
	} else {
		ipc_broadcast(ipc, SWL_IPC_EVENT_FOCUS,
			"event focus none\n", 18);
	}
}

void
swl_ipc_notify_window_open(SwlIpc *ipc, Client *c)
{
	if (!ipc || !c)
		return;
	char info[1024];
	format_client(info, sizeof(info), c, c->mon);
	ipc_broadcastf(ipc, SWL_IPC_EVENT_WINDOW_OPEN,
		"event window_open %s\n", info);
}

void
swl_ipc_notify_window_close(SwlIpc *ipc, Client *c)
{
	if (!ipc || !c)
		return;
	const char *title = swl_client_get_title(c);
	const char *appid = swl_client_get_appid(c);
	ipc_broadcastf(ipc, SWL_IPC_EVENT_WINDOW_CLOSE,
		"event window_close title=\"%s\" appid=\"%s\"\n",
		title ? title : "", appid ? appid : "");
}

void
swl_ipc_notify_layout(SwlIpc *ipc, Monitor *m)
{
	if (!ipc || !m)
		return;
	ipc_broadcastf(ipc, SWL_IPC_EVENT_LAYOUT,
		"event layout mon=\"%s\"\n",
		m->wlr_output->name);
}

void
swl_ipc_notify_fullscreen(SwlIpc *ipc, Client *c)
{
	if (!ipc || !c)
		return;
	char info[1024];
	format_client(info, sizeof(info), c, c->mon);
	ipc_broadcastf(ipc, SWL_IPC_EVENT_FULLSCREEN,
		"event fullscreen %s\n", info);
}

void
swl_ipc_notify_floating(SwlIpc *ipc, Client *c)
{
	if (!ipc || !c)
		return;
	char info[1024];
	format_client(info, sizeof(info), c, c->mon);
	ipc_broadcastf(ipc, SWL_IPC_EVENT_FLOATING,
		"event floating %s\n", info);
}

void
swl_ipc_notify_title(SwlIpc *ipc, Client *c)
{
	if (!ipc || !c)
		return;
	char info[1024];
	format_client(info, sizeof(info), c, c->mon);
	ipc_broadcastf(ipc, SWL_IPC_EVENT_TITLE,
		"event title %s\n", info);
}

void
swl_ipc_notify_monitor(SwlIpc *ipc, Monitor *m, int added)
{
	if (!ipc || !m)
		return;
	ipc_broadcastf(ipc, SWL_IPC_EVENT_MONITOR,
		"event monitor %s name=\"%s\" w=%d h=%d\n",
		added ? "added" : "removed",
		m->wlr_output->name,
		m->m.width, m->m.height);
}

void
swl_ipc_notify_config_reload(SwlIpc *ipc)
{
	if (!ipc)
		return;
	ipc_broadcast(ipc, SWL_IPC_EVENT_CONFIG,
		"event config_reload\n", 21);
}
