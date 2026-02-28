{
  description = "swl - dwm for Wayland compositor";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
    scenefx = {
      url = "github:wlrfx/scenefx";
      inputs.nixpkgs.follows = "nixpkgs";
    };
  };

  outputs = { self, nixpkgs, flake-utils, scenefx }:
    flake-utils.lib.eachSystem [ "x86_64-linux" "aarch64-linux" ] (system:
      let
        pkgs = nixpkgs.legacyPackages.${system};
        scenefx-git = scenefx.packages.${system}.default;

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
          }:

          stdenv.mkDerivation {
            pname = "swl";
            version = "0.8-dev";

            src = lib.fileset.toSource {
              root = ./.;
              fileset = lib.fileset.unions [
                ./meson.build
                ./meson.options
                ./src
                ./tools
                ./protocols
                ./config
                ./swl.1
                ./swl.desktop
              ];
            };

            strictDeps = true;

            outputs = [ "out" "man" ];

            nativeBuildInputs = [
              installShellFiles
              pkg-config
              wayland-scanner
              pkgs.meson
              pkgs.ninja
            ];

            buildInputs = [
              libinput
              libxcb
              libxkbcommon
              pixman
              scenefx-git
              wayland
              wayland-protocols
              wlroots_0_19
            ] ++ lib.optionals enableXWayland [
              xcbutilwm
              xwayland
            ];

            mesonFlags = lib.optionals enableXWayland [
              "-Dxwayland=enabled"
            ];

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
