{ pkgs ? import <nixpkgs> {}
, enableWlroots ? true
, enableStatic ? false
, enableMusl ? false
}:

let
  stdenv = if enableMusl then pkgs.pkgsStatic.stdenv else pkgs.stdenv;
  
  wlrootsDeps = if enableWlroots then with pkgs; [
    wayland
    wayland-protocols
    wlr-protocols
  ] else [];
  
in stdenv.mkDerivation rec {
  pname = "microshot";
  version = "1.0.0";

  src = ./.;

  nativeBuildInputs = with pkgs; [
    pkg-config
  ] ++ (if enableWlroots then [ wayland-scanner ] else []);

  buildInputs = with pkgs; [
    xorg.libX11
    libpng
    dbus
  ] ++ wlrootsDeps;

  buildPhase = let
    staticFlags = if enableStatic then "-static" else "";
    wlrootsFlags = if enableWlroots then "-DUSE_WLROOTS" else "";
    simdFlags = "-msse3 -mavx2";
  in ''
    ${if enableWlroots then ''
      # Generate wlr-screencopy protocol from wlr-protocols
      wayland-scanner client-header \
        ${pkgs.wlr-protocols}/share/wlr-protocols/unstable/wlr-screencopy-unstable-v1.xml \
        wlr-screencopy-unstable-v1-client-protocol.h
      wayland-scanner private-code \
        ${pkgs.wlr-protocols}/share/wlr-protocols/unstable/wlr-screencopy-unstable-v1.xml \
        wlr-screencopy-unstable-v1-client-protocol.c
    '' else ""}
    
    $CC -O3 -march=native -flto -fomit-frame-pointer \
        -ffast-math -funroll-loops -fprefetch-loop-arrays \
        ${simdFlags} ${staticFlags} ${wlrootsFlags} \
        $(pkg-config --cflags x11 libpng dbus-1 ${if enableWlroots then "wayland-client" else ""}) \
        microshot.c ${if enableWlroots then "wlr-screencopy-unstable-v1-client-protocol.c" else ""} \
        $(pkg-config --libs x11 libpng dbus-1 ${if enableWlroots then "wayland-client" else ""}) \
        -o microshot
    strip microshot
  '';

  installPhase = ''
    mkdir -p $out/bin
    cp microshot $out/bin/
  '';

  meta = with pkgs.lib; {
    description = "Ultra-fast screenshot utility with SIMD optimization";
    license = licenses.mit;
    platforms = platforms.linux;
  };
}
