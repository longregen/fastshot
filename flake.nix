{
  description = "Fastshot - Screenshot loop utility";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
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
      }
    )
    // {
      nixosModules = {
        default = self.nixosModules.screenshot-loop;
        screenshot-loop = ./nixos-module.nix;
      };
    };
}
