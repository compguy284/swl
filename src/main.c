/*
 * See LICENSE file for copyright and license details.
 */
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <wlr/util/log.h>

#include "config.h"
#include "server.h"
#include "util.h"

static char *
find_config(void)
{
	static char path[4096];
	const char *xdg_config = getenv("XDG_CONFIG_HOME");
	const char *home = getenv("HOME");

	if (xdg_config) {
		snprintf(path, sizeof(path), "%s/swl/config.toml", xdg_config);
		if (access(path, R_OK) == 0)
			return path;
	}
	if (home) {
		snprintf(path, sizeof(path), "%s/.config/swl/config.toml", home);
		if (access(path, R_OK) == 0)
			return path;
	}
	return nullptr;
}

int
main(int argc, char *argv[])
{
	SwlServer server = {0};
	char *startup_cmd = nullptr;
	char *config_path = nullptr;
	int c;

	swl_config_defaults(&server.config);

	while ((c = getopt(argc, argv, "c:s:hdv")) != -1) {
		if (c == 'c')
			config_path = optarg;
		else if (c == 's')
			startup_cmd = optarg;
		else if (c == 'd')
			server.config.log_level = WLR_DEBUG;
		else if (c == 'v')
			die("swl " VERSION);
		else
			goto usage;
	}
	if (optind < argc)
		goto usage;

	/* Load configuration: explicit path, XDG search, or built-in defaults */
	if (!config_path)
		config_path = find_config();
	if (config_path) {
		if (swl_config_load(&server.config, config_path) < 0)
			die("failed to load config: %s", config_path);
	}

	/* Wayland requires XDG_RUNTIME_DIR for creating its communications socket */
	if (!getenv("XDG_RUNTIME_DIR"))
		die("XDG_RUNTIME_DIR must be set");

	swl_server_setup(&server);
	swl_server_run(&server, startup_cmd);
	swl_server_cleanup(&server);
	return EXIT_SUCCESS;

usage:
	die("Usage: %s [-v] [-d] [-c config] [-s startup command]", argv[0]);
}
