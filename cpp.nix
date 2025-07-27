# default.nix ─ build the single‑file microshot.cpp
{ pkgs ? import <nixpkgs> { } }:

let
  stb_hdr = pkgs.fetchurl {
    url    = "https://raw.githubusercontent.com/nothings/stb/1ee679ca2ef753a528db5ba6801e1067b40481b8/stb_image_write.h";
    sha256 = "01aaj6v3q19jiqdcywr4q7r3901ksahm8qxkzy54dx4wganz1mfb";
  };
in pkgs.stdenv.mkDerivation {
  pname   = "microshot";
  version = "0.1.0";

  ## Your working dir (microshot/…)
  src = ./.;

  buildInputs = [
    pkgs.xorg.libX11
    pkgs.xorg.libXext
  ];

  dontConfigure = true;

  buildPhase = ''
    echo "Compiling microshot…"
    mkdir -p include
    cp ${stb_hdr} include/stb_image_write.h
    mkdir -p build
    ${pkgs.stdenv.cc}/bin/g++ \
      -O3 -pipe -std=c++17 -DNDEBUG \
      -Iinclude -Isrc \
      src/microshot.cpp \
      -lX11 -lXext \
      -o build/microshot
  '';

  installPhase = ''
    mkdir -p $out/bin
    cp build/microshot $out/bin/
  '';

  meta = with pkgs.lib; {
    description = "Ultra‑minimal, lightning‑fast screenshot CLI";
    license     = licenses.gpl3Plus;
    maintainers = [ maintainers.yourGithubHandle ];
    platforms   = platforms.linux;
  };
}

