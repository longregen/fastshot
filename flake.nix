{
  description = "FastShot - Quick PNG CLI screenshot utility for Plasma 6 with SIMD optimizations";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = nixpkgs.legacyPackages.${system};
        fastshot = pkgs.callPackage ./default.nix {};
      in
      {
        packages = {
          default = fastshot;
          fastshot = fastshot;
        };

        apps = {
          default = {
            type = "app";
            program = "${fastshot}/bin/fastshot";
          };
        };

        devShells.default = pkgs.mkShell {
          buildInputs = with pkgs; [
            pkg-config
            systemd
            libpng
            gcc
            valgrind
            dbus
            (python3.withPackages (ps: [ ps.dbus-python ps.pygobject3 ]))
          ];
        };

        checks = {
          # Run the VM test as a check
          vm-test = import ./test.nix { inherit pkgs; };

          # Run valgrind memory safety test
          valgrind-test = import ./valgrind-test.nix { inherit pkgs; };
        };
      }
    ) // {
      # NixOS module output (system-independent)
      nixosModules.default = { config, lib, pkgs, ... }: {
        options.services.fastshot = {
          enable = lib.mkEnableOption "FastShot screenshot utility with KWin DBus permissions";
        };

        config = lib.mkIf config.services.fastshot.enable {
          environment.systemPackages = [
            (let
              fastshot = pkgs.callPackage ./default.nix {};
              fastshotDesktop = pkgs.makeDesktopItem {
                name = "fastshot";
                desktopName = "FastShot";
                noDisplay = true;
                exec = "${fastshot}/bin/fastshot";
                extraConfig = {
                  "X-KDE-DBUS-Restricted-Interfaces" = "org.kde.KWin.ScreenShot2";
                };
              };
            in
              pkgs.runCommand "fastshot-with-desktop" {} ''
                mkdir -p $out/bin $out/share/applications

                # Copy binary
                cp -r ${fastshot}/bin/* $out/bin/

                # Copy desktop file with explicit permissions
                cp ${fastshotDesktop}/share/applications/*.desktop $out/share/applications/
                chmod 644 $out/share/applications/*.desktop
              '')
          ];
        };
      };

      # Alias for convenience
      nixosModules.fastshot = self.nixosModules.default;
    };
}
