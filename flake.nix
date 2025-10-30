{
  description = "Fastshot - Screenshot loop utility";

  inputs = {
    nixpkgs.url = "git+ssh://gitea/mirrors/nixpkgs?shallow=1&ref=nixos-unstable";
    flake-utils.url = "git+ssh://gitea/mirrors/flake-utils";
  };

  outputs =
    {
      self,
      nixpkgs,
      flake-utils,
    }:
    flake-utils.lib.eachDefaultSystem (
      system:
      let
        pkgs = nixpkgs.legacyPackages.${system};
      in
      {
        packages = {
          default = self.packages.${system}.fastshot;

          fastshot = pkgs.callPackage ./package.nix { };

          fastshotWithDesktop = pkgs.symlinkJoin {
            name = "fastshot-with-desktop";
            paths = [
              self.packages.${system}.fastshot
              (pkgs.makeDesktopItem {
                name = "fastshot";
                desktopName = "FastShot";
                noDisplay = true;
                exec = "${self.packages.${system}.fastshot}/bin/fastshot";
                extraConfig = {
                  "X-KDE-DBUS-Restricted-Interfaces" = "org.kde.KWin.ScreenShot2";
                };
              })
            ];
          };
        };

        checks = {
          vm-test = pkgs.callPackage ./vm-test.nix {
            inherit (self.packages.${system}) fastshot fastshotWithDesktop;
          };
        };
      }
    )
    // {
      nixosModules = {
        default = self.nixosModules.screenshot-loop;
        screenshot-loop = ./nixos-module.nix;
      };
    };
}
