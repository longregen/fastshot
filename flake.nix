{
  description = "FastShot - Quick PNG CLI screenshot utility for Plasma 6 with SIMD optimizations";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = {self, nixpkgs, flake-utils}:
    flake-utils.lib.eachDefaultSystem (system: let
      pkgs = nixpkgs.legacyPackages.${system};
      fastshot = pkgs.callPackage ./default.nix {};
    in {
      packages = {
        default = fastshot;
        fastshot = fastshot;
        bin = fastshot.passthru.bin;
        desktop = fastshot.passthru.desktop;
      };

      apps.default = {
        type = "app";
        program = "${fastshot}/bin/fastshot";
      };

      devShells.default = pkgs.mkShell {
        buildInputs = with pkgs; [
          pkg-config
          systemd
          libpng
          gcc
          valgrind
          dbus
          dex
        ];
      };

      checks = {
        vm-test = import ./tests/vm.nix {inherit pkgs;};
        valgrind-test = import ./tests/valgrind.nix {inherit pkgs;};
      };
    })
    // {
      nixosModules.default = import ./module.nix;

      nixosModules.fastshot = self.nixosModules.default;
    };
}
