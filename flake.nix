{
  description = "Minimal kwin-screenshot helper";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-24.05";

  outputs = { self, nixpkgs }:
  let
    system = "x86_64-linux";                        # adjust if you need aarch64‑linux, …
    pkgs   = import nixpkgs { inherit system; };
    commonFlags = "-O2 -s";                         # strip + optimise a bit
  in {
    ############################################################
    # 1)  DYNAMICALLY‑LINKED           (≈ 35 kB on glibc)
    ############################################################
    packages.${system}.fastshot = pkgs.stdenv.mkDerivation {
      pname   = "fastshot";
      version = "0.1.0";

      src = ./.;                                   # expects fastshot.c in the cwd
      buildInputs = [ pkgs.pkg-config pkgs.systemd ];

      buildPhase = ''
        gcc ${commonFlags} fastshot.c \
            $(pkg-config --cflags --libs libsystemd) \
            -o fastshot
      '';

      installPhase = ''
        mkdir -p $out/bin
        cp fastshot $out/bin/
      '';
    };

    ############################################################
    # 2)  FULLY‑STATIC (musl)            (≈ 150 kB)
    ############################################################
    packages.${system}.fastshotStatic =
    let
      staticPkgs = import nixpkgs {
        inherit system;
        overlays = [ (final: prev: {                             # pull musl + static systemd
          systemd = prev.pkgsStatic.systemd;
        }) ];
        pkgsStatic = true;
      };
    in staticPkgs.stdenv.mkDerivation {
       pname   = "fastshotStatic";
       version = "0.1.0";
       src     = ./.;
       buildInputs = [ staticPkgs.pkg-config staticPkgs.systemd ];

       NIX_CFLAGS_COMPILE = "-static ${commonFlags}";
       NIX_LDFLAGS        = "-static";

       buildPhase = ''
         ${staticPkgs.cc}/bin/gcc $NIX_CFLAGS_COMPILE fastshot.c \
           $(pkg-config --cflags --libs libsystemd) \
           -o fastshot
       '';

       installPhase = ''
         mkdir -p $out/bin
         cp fastshot $out/bin/
       '';
    };

    ############################################################
    # 3)  Dev shell  (nix develop)
    ############################################################
    devShells.${system}.default = pkgs.mkShell {
      packages = [ pkgs.pkg-config pkgs.systemd pkgs.gcc ];
      shellHook = ''
        echo "fastshot dev‑shell – edit fastshot.c then:  gcc -o fastshot fastshot.c \\
$(pkg-config --cflags --libs libsystemd)"
      '';
    };
  };
}

