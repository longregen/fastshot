{
  pkgs,
  lib,
  config,
  ...
}:
let
  cfg = config.behaviors.screenshot-loop;

  # Convert systemd time format to seconds for fastshot-loop
  frequencyToSeconds =
    freq:
    let
      match = builtins.match "([0-9]+)([smh]?)" freq;
      num = lib.toInt (builtins.elemAt match 0);
      unit = builtins.elemAt match 1;
    in
    if unit == "m" then
      num * 60
    else if unit == "h" then
      num * 3600
    else
      num; # default to seconds

in
{
  options.behaviors.screenshot-loop = {
    enable = lib.mkOption {
      type = lib.types.bool;
      default = false;
      description = "Whether to take a screenshot every few seconds";
    };
    user = lib.mkOption {
      type = lib.types.nullOr lib.types.str;
      default = null;
      description = "User for the service";
    };
    frequency = lib.mkOption {
      type = lib.types.str;
      default = "45s";
      description = "Frequency ([s]econds, [m]inutes, or [h]ours)";
      example = "4m";
    };
    threshold = lib.mkOption {
      type = lib.types.float;
      default = 0.99;
      description = "Similarity threshold (0-1)";
    };
  };
  config = lib.mkIf cfg.enable {
    systemd.user.services.screenshot-loop = {
      enable = true;
      description = "Screenshot Loop Service";
      wants = [ "graphical-session.target" ];
      after = [
        "graphical-session.target"
        "plasma-kwin_x11.service"
        "plasma-kwin_wayland.service"
      ];
      partOf = [ "graphical-session.target" ];
      wantedBy = [ "graphical-session.target" ];

      serviceConfig = {
        Type = "simple";
        ExecStart = "${pkgs.fastshotWithDesktop}/bin/fastshot --loop -d %h/desktop-record -i ${toString (frequencyToSeconds cfg.frequency)} -t ${toString cfg.threshold} -v";
        Restart = "on-failure";
        RestartSec = "5";

        ExecStartPre = [
          "${pkgs.systemd}/bin/systemctl --user import-environment DISPLAY WAYLAND_DISPLAY DBUS_SESSION_BUS_ADDRESS XDG_RUNTIME_DIR"
          "${pkgs.coreutils}/bin/sleep 10" # TODO: instead of 10 seconds, can we test that the dbus session is ready or something?
        ];

        # Run in the same slice as KDE to inherit permissions
        Slice = "app.slice";

        # Ensure we have access to the display
        PrivateDevices = false;
        ProtectHome = false;
      };
    };
  };
}
