/*
 * swlmsg — command-line IPC client for swl compositor.
 *
 * Usage:
 *   swlmsg cmd focusstack 1
 *   swlmsg cmd killclient
 *   swlmsg query clients
 *   swlmsg query focused
 *   swlmsg subscribe focus,window_open
 *
 * Exit codes: 0 on "ok", 1 on "error" or connection failure.
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

static int
connect_socket(void)
{
	const char *sock_path = getenv("SWL_SOCK");
	char pathbuf[256];

	if (!sock_path) {
		const char *runtime_dir = getenv("XDG_RUNTIME_DIR");
		const char *wayland_display = getenv("WAYLAND_DISPLAY");
		if (!runtime_dir || !wayland_display) {
			fprintf(stderr, "swlmsg: SWL_SOCK, XDG_RUNTIME_DIR, or WAYLAND_DISPLAY not set\n");
			return -1;
		}
		snprintf(pathbuf, sizeof(pathbuf), "%s/swl-%s.sock",
			runtime_dir, wayland_display);
		sock_path = pathbuf;
	}

	int fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0) {
		perror("swlmsg: socket");
		return -1;
	}

	struct sockaddr_un addr = { .sun_family = AF_UNIX };
	if (strlen(sock_path) >= sizeof(addr.sun_path)) {
		fprintf(stderr, "swlmsg: socket path too long\n");
		close(fd);
		return -1;
	}
	strncpy(addr.sun_path, sock_path, sizeof(addr.sun_path) - 1);

	if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		fprintf(stderr, "swlmsg: connect to %s: %s\n", sock_path, strerror(errno));
		close(fd);
		return -1;
	}

	return fd;
}

static void
usage(FILE *f)
{
	fprintf(f,
		"usage: swlmsg <command> [args...]\n"
		"\n"
		"Commands:\n"
		"  cmd <action> [arg]             Execute a compositor action\n"
		"  query <target>                 Query compositor state\n"
		"  subscribe <event>[,event,...]  Subscribe to event stream\n"
		"\n"
		"Actions:\n"
		"  focusstack <+1|-1>             Focus next/previous client\n"
		"  focusdir <0-3>                 Focus left/right/up/down\n"
		"  focusmon <left|right>          Focus adjacent monitor\n"
		"  killclient                     Close focused client\n"
		"  togglefloating                 Toggle floating state\n"
		"  togglefullscreen               Toggle fullscreen state\n"
		"  tagmon <left|right>            Move client to adjacent monitor\n"
		"  swapdir <0-3>                  Swap left/right/up/down\n"
		"  consume_or_expel <-1|1>        Consume/expel column left/right\n"
		"  scroller_cycle_width <+1|-1>   Cycle column width preset\n"
		"  scroller_set_width <float>     Set column width (fraction or px)\n"
		"  reload_config                  Reload configuration file\n"
		"  chvt <1-12>                    Switch virtual terminal\n"
		"  quit                           Exit compositor\n"
		"\n"
		"Query targets:\n"
		"  clients                        List all clients\n"
		"  monitors                       List all monitors\n"
		"  focused                        Show focused client\n"
		"  keybinds                       List all key bindings\n"
		"\n"
		"Events:\n"
		"  focus          Focus changed\n"
		"  window_open    Client mapped\n"
		"  window_close   Client unmapped\n"
		"  layout         Layout rearranged\n"
		"  fullscreen     Fullscreen state changed\n"
		"  floating       Floating state changed\n"
		"  title          Client title changed\n"
		"  monitor        Monitor added/removed\n"
		"  config_reload  Config was reloaded\n"
		"\n"
		"Environment:\n"
		"  SWL_SOCK              Socket path (overrides default)\n"
		"  XDG_RUNTIME_DIR       Used to construct default socket path\n"
		"  WAYLAND_DISPLAY       Used to construct default socket path\n"
		"\n"
		"Socket path: $XDG_RUNTIME_DIR/swl-$WAYLAND_DISPLAY.sock\n"
		"Exit codes: 0 on success, 1 on error or connection failure.\n"
	);
}

int
main(int argc, char *argv[])
{
	if (argc < 2) {
		usage(stderr);
		return 1;
	}

	if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
		usage(stdout);
		return 0;
	}

	int fd = connect_socket();
	if (fd < 0)
		return 1;

	/* Build request line from argv[1..] */
	char req[4096];
	size_t off = 0;
	for (int i = 1; i < argc; i++) {
		size_t alen = strlen(argv[i]);
		if (off + alen + 2 > sizeof(req)) {
			fprintf(stderr, "swlmsg: request too long\n");
			close(fd);
			return 1;
		}
		if (i > 1)
			req[off++] = ' ';
		memcpy(req + off, argv[i], alen);
		off += alen;
	}
	req[off++] = '\n';

	/* Send request */
	if (write(fd, req, off) < 0) {
		perror("swlmsg: write");
		close(fd);
		return 1;
	}

	/* Determine if this is a subscribe (long-running) */
	int is_subscribe = (strcmp(argv[1], "subscribe") == 0);

	/* Read and print response lines */
	char buf[4096];
	size_t buf_len = 0;
	int exit_code = 0;

	for (;;) {
		ssize_t n = read(fd, buf + buf_len, sizeof(buf) - buf_len);
		if (n <= 0)
			break;
		buf_len += (size_t)n;

		/* Process complete lines */
		char *start = buf;
		char *nl;
		while ((nl = memchr(start, '\n', buf_len - (size_t)(start - buf))) != NULL) {
			*nl = '\0';
			size_t line_len = (size_t)(nl - start);

			if (!is_subscribe) {
				if (strcmp(start, "ok") == 0) {
					close(fd);
					return 0;
				}
				if (strncmp(start, "error ", 6) == 0) {
					fprintf(stderr, "%s\n", start);
					close(fd);
					return 1;
				}
				if (strcmp(start, "end") == 0) {
					close(fd);
					return 0;
				}
			}

			if (line_len > 0)
				printf("%s\n", start);
			fflush(stdout);
			start = nl + 1;
		}

		/* Move remaining partial line to front */
		size_t remaining = buf_len - (size_t)(start - buf);
		if (remaining > 0 && start != buf)
			memmove(buf, start, remaining);
		buf_len = remaining;
	}

	close(fd);
	return exit_code;
}
