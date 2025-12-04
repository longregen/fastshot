{pkgs ? import <nixpkgs> {}}:
pkgs.stdenv.mkDerivation {
  pname = "fastshot";
  version = "0.1.0";
  src = ./src;
  nativeBuildInputs = [pkgs.pkg-config];
  buildInputs = [pkgs.systemd pkgs.libpng];
  CFLAGS = "-O3 -pipe -msse2 -mavx2 -fno-plt -fomit-frame-pointer -ftree-vectorize";
  buildPhase = ''
    gcc $CFLAGS fastshot.c \
      $(pkg-config --cflags --libs libsystemd libpng) \
      -o fastshot
  '';
  installPhase = ''
    install -Dm755 fastshot $out/bin/fastshot
  '';
  meta = with pkgs.lib; {
    description = "Quick PNG CLI screenshot utility for Plasma 6 with SIMD optimizations";
    license = licenses.mit;
    platforms = platforms.linux;
  };
}
