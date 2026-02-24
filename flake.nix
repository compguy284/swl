{
  description = "swl - dwm for Wayland compositor";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachSystem [ "x86_64-linux" "aarch64-linux" ] (system:
      let
        pkgs = nixpkgs.legacyPackages.${system};

        swl = pkgs.callPackage (
          { lib
          , stdenv
          , installShellFiles
          , pkg-config
          , wayland-scanner
          , libinput
          , libxcb
          , libxkbcommon
          , pixman
          , wayland
          , wayland-protocols
          , wlroots_0_19
          , xcbutilwm
          , xwayland
          , enableXWayland ? false
          , configH ? null
          }:

          stdenv.mkDerivation {
            pname = "swl";
            version = "0.8-dev";

            src = lib.fileset.toSource {
              root = ./.;
              fileset = lib.fileset.unions [
                ./Makefile
                ./config.mk
                ./config.def.h
                ./client.h
                ./swl.c
                ./swl.1
                ./swl.desktop
                ./util.c
                ./util.h
                ./protocols
              ];
            };

            __structuredAttrs = true;
            strictDeps = true;

            outputs = [ "out" "man" ];

            nativeBuildInputs = [
              installShellFiles
              pkg-config
              wayland-scanner
            ];

            buildInputs = [
              libinput
              libxcb
              libxkbcommon
              pixman
              wayland
              wayland-protocols
              wlroots_0_19
            ] ++ lib.optionals enableXWayland [
              xcbutilwm
              xwayland
            ];

            makeFlags = [
              "PKG_CONFIG=${stdenv.cc.targetPrefix}pkg-config"
              "WAYLAND_SCANNER=wayland-scanner"
              "PREFIX=$(out)"
              "MANDIR=$(man)/share/man"
            ] ++ lib.optionals enableXWayland [
              "XWAYLAND=-DXWAYLAND"
              "XLIBS=xcb xcb-icccm"
            ];

            postPatch = lib.optionalString (configH != null) ''
              cp ${configH} config.h
            '';

            passthru = {
              inherit enableXWayland;
            };

            meta = {
              description = "swl - dwm for Wayland compositor";
              homepage = "https://codeberg.org/mcguyver/swl";
              license = lib.licenses.gpl3Only;
              platforms = lib.platforms.linux;
              mainProgram = "swl";
            };
          }
        ) {};

      in {
        packages = {
          default = swl;
          swl = swl;
          swl-xwayland = swl.override { enableXWayland = true; };
        };

        devShells.default = pkgs.mkShell {
          inputsFrom = [ swl ];
          packages = with pkgs; [
            gdb
            valgrind
            wayland-utils
          ];
        };
      }
    ) // {
      nixosModules.default = import ./nix/module.nix self;
    };
}
