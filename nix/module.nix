self:

{ config, lib, pkgs, ... }:

let
  cfg = config.programs.swl;
in
{
  options.programs.swl = {
    enable = lib.mkEnableOption "swl, a dwm for Wayland compositor";

    package = lib.mkPackageOption self.packages.${pkgs.stdenv.hostPlatform.system} "swl" {
      default = "default";
      extraDescription = ''
        Override to enable XWayland, e.g.:
        `self.packages.''${system}.default.override { enableXWayland = true; }`
      '';
    };

    configFile = lib.mkOption {
      type = lib.types.nullOr lib.types.path;
      default = null;
      description = ''
        Path to a TOML configuration file for swl.
        If null, swl will search the default XDG config path
        or use built-in defaults.
      '';
    };

    extraSessionCommands = lib.mkOption {
      type = lib.types.lines;
      default = "";
      example = ''
        export XDG_CURRENT_DESKTOP=swl
      '';
      description = ''
        Shell commands executed just before swl is started.
        Useful for setting session environment variables.
      '';
    };
  };

  config = lib.mkIf cfg.enable {
    environment.systemPackages = [ cfg.package ];

    systemd.user.targets.swl-session = {
      description = "swl compositor session";
      bindsTo = [ "graphical-session.target" ];
      wants = [ "graphical-session-pre.target" ];
      after = [ "graphical-session-pre.target" ];
    };

    services.displayManager.sessionPackages = [
      (pkgs.runCommand "swl-session-desktop" {
        passthru.providedSessions = [ "swl" ];
      } ''
        mkdir -p $out/share/wayland-sessions
        cat > $out/share/wayland-sessions/swl.desktop <<EOF
        [Desktop Entry]
        Name=swl
        Comment=dwm for Wayland
        Exec=/etc/xdg/swl-session
        Type=Application
        EOF
      '')
    ];

    environment.etc."xdg/swl-session" = {
      mode = "0555";
      text = ''
        #!/bin/sh
        ${cfg.extraSessionCommands}
        systemctl --user import-environment DISPLAY WAYLAND_DISPLAY
        systemctl --user start swl-session.target
        exec ${lib.getExe cfg.package}${lib.optionalString (cfg.configFile != null) " -c ${cfg.configFile}"}
      '';
    };

    xdg.portal.config.swl.default = [ "wlr" "gtk" ];
  };
}
