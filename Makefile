.POSIX:
.SUFFIXES:

include config.mk

# flags for compiling
SWLCPPFLAGS = -I. -DWLR_USE_UNSTABLE -D_POSIX_C_SOURCE=200809L \
	-DVERSION=\"$(VERSION)\" $(XWAYLAND)
SWLDEVCFLAGS = -g -Wpedantic -Wall -Wextra -Wdeclaration-after-statement \
	-Wno-unused-parameter -Wshadow -Wunused-macros -Werror=strict-prototypes \
	-Werror=implicit -Werror=return-type -Werror=incompatible-pointer-types \
	-Wfloat-conversion

# CFLAGS / LDFLAGS
PKGS      = wayland-server xkbcommon libinput $(XLIBS)
SWLCFLAGS = `$(PKG_CONFIG) --cflags $(PKGS)` $(WLR_INCS) $(SWLCPPFLAGS) $(SWLDEVCFLAGS) $(CFLAGS)
LDLIBS    = `$(PKG_CONFIG) --libs $(PKGS)` $(WLR_LIBS) -lm $(LIBS)

all: swl
swl: swl.o util.o
	$(CC) swl.o util.o $(SWLCFLAGS) $(LDFLAGS) $(LDLIBS) -o $@
swl.o: swl.c client.h config.h config.mk cursor-shape-v1-protocol.h \
	pointer-constraints-unstable-v1-protocol.h wlr-layer-shell-unstable-v1-protocol.h \
	wlr-output-power-management-unstable-v1-protocol.h xdg-shell-protocol.h
util.o: util.c util.h

# wayland-scanner is a tool which generates C headers and rigging for Wayland
# protocols, which are specified in XML. wlroots requires you to rig these up
# to your build system yourself and provide them in the include path.
WAYLAND_SCANNER   = `$(PKG_CONFIG) --variable=wayland_scanner wayland-scanner`
WAYLAND_PROTOCOLS = `$(PKG_CONFIG) --variable=pkgdatadir wayland-protocols`

cursor-shape-v1-protocol.h:
	$(WAYLAND_SCANNER) enum-header \
		$(WAYLAND_PROTOCOLS)/staging/cursor-shape/cursor-shape-v1.xml $@
pointer-constraints-unstable-v1-protocol.h:
	$(WAYLAND_SCANNER) enum-header \
		$(WAYLAND_PROTOCOLS)/unstable/pointer-constraints/pointer-constraints-unstable-v1.xml $@
wlr-layer-shell-unstable-v1-protocol.h:
	$(WAYLAND_SCANNER) enum-header \
		protocols/wlr-layer-shell-unstable-v1.xml $@
wlr-output-power-management-unstable-v1-protocol.h:
	$(WAYLAND_SCANNER) server-header \
		protocols/wlr-output-power-management-unstable-v1.xml $@
xdg-shell-protocol.h:
	$(WAYLAND_SCANNER) server-header \
		$(WAYLAND_PROTOCOLS)/stable/xdg-shell/xdg-shell.xml $@

config.h:
	cp config.def.h $@
clean:
	rm -f swl *.o *-protocol.h

dist: clean
	mkdir -p swl-$(VERSION)
	cp -R LICENSE* Makefile CHANGELOG.md README.md client.h config.def.h \
		config.mk protocols swl.1 swl.c util.c util.h swl.desktop \
		swl-$(VERSION)
	tar -caf swl-$(VERSION).tar.gz swl-$(VERSION)
	rm -rf swl-$(VERSION)

install: swl
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	rm -f $(DESTDIR)$(PREFIX)/bin/swl
	cp -f swl $(DESTDIR)$(PREFIX)/bin
	chmod 755 $(DESTDIR)$(PREFIX)/bin/swl
	mkdir -p $(DESTDIR)$(MANDIR)/man1
	cp -f swl.1 $(DESTDIR)$(MANDIR)/man1
	chmod 644 $(DESTDIR)$(MANDIR)/man1/swl.1
	mkdir -p $(DESTDIR)$(DATADIR)/wayland-sessions
	cp -f swl.desktop $(DESTDIR)$(DATADIR)/wayland-sessions/swl.desktop
	chmod 644 $(DESTDIR)$(DATADIR)/wayland-sessions/swl.desktop
uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/swl $(DESTDIR)$(MANDIR)/man1/swl.1 \
		$(DESTDIR)$(DATADIR)/wayland-sessions/swl.desktop

.SUFFIXES: .c .o
.c.o:
	$(CC) $(CPPFLAGS) $(SWLCFLAGS) -o $@ -c $<
