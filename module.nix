{pkgs, lib, config, ...}: let
  cfg = config.services.fastshot;
  fastshot = pkgs.callPackage ./default.nix {};
in {
  options.services.fastshot = {
    enable = lib.mkEnableOption "FastShot continuous screenshot service";

    user = lib.mkOption {
      type = lib.types.str;
      description = "User to run the fastshot service for";
    };

    directory = lib.mkOption {
      type = lib.types.str;
      default = "desktop-record";
      description = "Directory under $HOME to store screenshots";
    };

    interval = lib.mkOption {
      type = lib.types.int;
      default = 45;
      description = "Seconds between screenshots";
    };

    threshold = lib.mkOption {
      type = lib.types.float;
      default = 0.99;
      description = "Similarity threshold (0-1) - only save if below this";
    };

    verbose = lib.mkOption {
      type = lib.types.bool;
      default = false;
      description = "Enable verbose logging";
    };
  };

  config = lib.mkIf cfg.enable {
    environment.systemPackages = [fastshot];

    systemd.user.services.fastshot = {
      description = "FastShot continuous screenshot capture";
      wantedBy = ["graphical-session.target"];
      after = ["graphical-session.target"];
      unitConfig = {
        ConditionUser = cfg.user;
      };
      serviceConfig = {
        Type = "simple";
        Restart = "on-failure";
        RestartSec = 5;
        ExecStart = ''
          ${fastshot}/bin/fastshot --loop \
            --directory %h/${cfg.directory} \
            --interval ${toString cfg.interval} \
            --threshold ${toString cfg.threshold} \
            ${lib.optionalString cfg.verbose "--verbose"}
        '';
      };
    };
  };
}
