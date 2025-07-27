{pkgs, ...}: let
  commonFlags = "-O2 -s";
in
  pkgs.stdenv.mkDerivation {
    pname = "fastshot";
    version = "0.1.0";

    src = ./src;
    buildInputs = [pkgs.pkg-config pkgs.systemd];

    buildPhase = ''
      gcc ${commonFlags} fastshot.c \
          $(pkg-config --cflags --libs libsystemd) \
          -o fastshot
    '';

    installPhase = ''
      mkdir -p $out/bin
      cp fastshot $out/bin/
    '';
  }
