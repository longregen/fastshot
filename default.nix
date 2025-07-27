{ pkgs ? import <nixpkgs> {} }:

pkgs.stdenv.mkDerivation rec {
  pname = "microshot";
  version = "1.0.0";

  src = ./.;

  nativeBuildInputs = with pkgs; [
    pkg-config
  ];

  buildInputs = with pkgs; [
    xorg.libX11
    libpng
    dbus
  ];

  buildPhase = ''
    $CC -O3 -march=native -flto -fomit-frame-pointer \
        -ffast-math -funroll-loops -fprefetch-loop-arrays \
        $(pkg-config --cflags x11 libpng dbus-1) \
        microshot.c \
        $(pkg-config --libs x11 libpng dbus-1) \
        -o microshot
    strip microshot
  '';

  installPhase = ''
    mkdir -p $out/bin
    cp microshot $out/bin/
  '';

  meta = with pkgs.lib; {
    description = "Minimal screenshot utility";
    license = licenses.mit;
    platforms = platforms.linux;
  };
}
