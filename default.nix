{pkgs ? import <nixpkgs> {}}: let
  stb-header = pkgs.fetchurl {
    url = "https://raw.githubusercontent.com/nothings/stb/1ee679ca2ef753a528db5ba6801e1067b40481b8/stb_image_write.h";
    sha256 = "01aaj6v3q19jiqdcywr4q7r3901ksahm8qxkzy54dx4wganz1mfb";
  };
in
  pkgs.stdenv.mkDerivation {
    pname = "fastshot";
    version = "0.1.0";
    src = ./src;
    nativeBuildInputs = [pkgs.pkg-config];
    buildInputs = [pkgs.systemd];

    CFLAGS = "-O2 -pipe -march=native -fno-plt -fomit-frame-pointer";

    buildPhase = ''
      cp ${stb-header} ./stb_image_write.h
      gcc $CFLAGS fastshot.c \
        $(pkg-config --cflags --libs libsystemd) \
        -o fastshot
    '';

    installPhase = ''
      install -Dm755 fastshot $out/bin/fastshot
    '';

    meta = with pkgs.lib; {
      description = "Quick PNG CLI screenshot utility for Plasma 6";
      license = licenses.mit;
      platforms = platforms.linux;
    };
  }
