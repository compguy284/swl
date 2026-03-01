# swl - a hackable Wayland compositor

swl is a compact, hackable compositor for [Wayland] built on [wlroots] and
[scenefx]. It is a fork of [dwl], extending it with runtime configuration,
a scrollable column layout, visual effects, IPC, and more -- while preserving
the spirit of a small, understandable codebase.

## Features

- **Scroller layout** -- a scrollable column-based layout where windows are
  arranged in columns that scroll horizontally, with support for stacking
  multiple windows within a single column
- **Runtime TOML configuration** -- no recompilation needed, reload with a
  keybinding or SIGHUP
- **Visual effects** via [scenefx] -- rounded corners, window shadows, opacity
- **IPC** -- Unix socket interface with the `swlmsg` client for scripting and
  external tooling
- **Directional focus and swap** -- navigate and rearrange windows by direction
- **Lid switch handling** -- run a command (e.g. screen locker) on laptop lid
  close
- **Foreign toplevel management** -- protocol support for task switchers and
  docks
- **D-Bus session integration** -- automatic environment import and session
  target lifecycle
- **XWayland support** (optional)

## Building

swl uses [Meson] and requires the following dependencies:

- wlroots-0.19
- scenefx-0.4
- wayland-server, wayland-protocols
- xkbcommon
- libinput

Optional (for XWayland):
- libxcb, xcb-icccm, Xwayland

### With Nix

```sh
nix develop
meson setup build
ninja -C build
```

### Without Nix

```sh
meson setup build
ninja -C build
sudo ninja -C build install
```

To enable XWayland:

```sh
meson setup build -Dxwayland=enabled
```

## Configuration

Copy the default config and customize it:

```sh
mkdir -p ~/.config/swl
cp /usr/share/swl/default.toml ~/.config/swl/config.toml
```

Configuration is in TOML and covers appearance, keybindings, trackpad
settings, monitor rules, effects, and more. Changes can be reloaded at runtime
without restarting the compositor.

See `config/default.toml` for all available options.

## Running

swl can run on any wlroots-supported backend -- directly from a VT console, or
nested inside an existing X11 or Wayland session. Depending on your setup, you
may need to be in the `video` and `input` groups, or have `seatd` /
`systemd-logind` / `elogind` running.

```sh
swl                     # start the compositor
swl -s 'foot --server'  # start with a startup command
```

The `-s` command receives status information on stdin (selected tags, window
title, layout). This can be piped to a status bar. If your startup command
does not consume stdin, close it:

```sh
swl -s 'foot --server <&-'
```

## IPC

swl exposes a Unix socket at `$XDG_RUNTIME_DIR/swl-$WAYLAND_DISPLAY.sock`.
Use the included `swlmsg` tool to send commands:

```sh
swlmsg spawn foot
swlmsg focusdir right
```

## Acknowledgements

swl is a fork of [dwl], created by Devin J. Pohly and the dwl community. dwl
itself began by extending the TinyWL example provided (CC0) by the
sway/wlroots developers.

Many thanks to:

- **[dwl]** and its contributors for creating the foundation this project
  builds on
- **Devin J. Pohly** for creating and nurturing dwl
- **suckless.org** and the [dwm] developers for the original inspiration
- The **sway/wlroots** developers for the compositor library and TinyWL
- **Alexander Courtis** for the XWayland implementation in dwl
- **Guido Cella** for the layer-shell implementation, patch maintenance, and
  helping keep dwl running
- **Stivvo** for output management, fullscreen support, and patch maintenance

## License

GPLv3 -- see [LICENSE](LICENSE). See also [LICENSE.dwm](LICENSE.dwm),
[LICENSE.sway](LICENSE.sway), and [LICENSE.tinywl](LICENSE.tinywl).

[Wayland]: https://wayland.freedesktop.org/
[wlroots]: https://gitlab.freedesktop.org/wlroots/wlroots/
[scenefx]: https://github.com/wlrfx/scenefx
[dwl]: https://codeberg.org/dwl/dwl
[dwm]: https://dwm.suckless.org/
[Meson]: https://mesonbuild.com/
