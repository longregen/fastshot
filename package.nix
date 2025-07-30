{
  pkgs ? import <nixpkgs>,
  ...
}:
let
  enableValgrind = false;
  generate-profile = false;
  use-profile = false;
in
pkgs.stdenv.mkDerivation {
  pname = "fastshot";
  version = "0.1.0";
  src = ./src;
  nativeBuildInputs = [ pkgs.pkg-config ];
  buildInputs = [
    pkgs.systemd
    pkgs.libpng
    pkgs.ffmpeg_7
  ];

  NIX_CFLAGS_COMPILE =
    if enableValgrind then
      "-O0 -g3 -ggdb -fno-omit-frame-pointer -Wall -Wextra -Wuninitialized -Wmaybe-uninitialized"
    else
      "-O3 -march=haswell -mtune=haswell -pipe -fno-plt -fomit-frame-pointer -ftree-vectorize";

  LDFLAGS =
    "-Wl,-O1 -Wl,--as-needed -lpthread"
    + (
      if enableValgrind then
        ""
      else if generate-profile then
        " -fprofile-generate -flto=auto"
      else if use-profile then
        " -fprofile-use -fuse-linker-plugin -flto=auto"
      else
        " -flto=auto"
    );

  buildPhase = ''
    # Build shared image comparison module
    gcc $NIX_CFLAGS_COMPILE -c image-compare.c \
      $(pkg-config --cflags libavutil) \
      -o image-compare.o

    # Build fastshot
    gcc $NIX_CFLAGS_COMPILE $LDFLAGS fastshot.c image-compare.o \
      $(pkg-config --cflags --libs libsystemd libpng libavutil) \
      -o fastshot

  '';
  installPhase = ''
    install -Dm755 fastshot $out/bin/fastshot
  '';

  doCheck = true;
  checkPhase = ''
    echo "Running image comparison unit tests..."
    gcc $NIX_CFLAGS_COMPILE test-image-compare.c image-compare.o \
      $(pkg-config --cflags --libs libavutil) \
      -o test-image-compare -lm
    ./test-image-compare
  '';

  meta = with pkgs.lib; {
    description = "Fast screenshot utilities for Plasma 6 with SIMD optimizations";
    license = licenses.mit;
    platforms = platforms.linux;
    mainProgram = "fastshot";
  };
}
