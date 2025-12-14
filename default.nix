{pkgs ? import <nixpkgs> {}}:
let
  # The C binary only
  fastshot-bin = pkgs.stdenv.mkDerivation {
    pname = "fastshot";
    version = "0.1.0";
    src = ./src;
    nativeBuildInputs = [pkgs.pkg-config];
    buildInputs = [pkgs.systemd pkgs.libpng];
    CFLAGS = "-O3 -pipe -msse2 -mavx2 -fno-plt -fomit-frame-pointer -ftree-vectorize";
    buildPhase = ''
      gcc $CFLAGS fastshot.c image-compare.c \
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
  };

  # Desktop file with KWin DBus permission
  fastshot-desktop = pkgs.makeDesktopItem {
    name = "fastshot";
    desktopName = "FastShot";
    noDisplay = true;
    exec = "${fastshot-bin}/bin/fastshot %f";
    extraConfig = {
      "X-KDE-DBUS-Restricted-Interfaces" = "org.kde.KWin.ScreenShot2";
    };
  };

  # Combined package: binary + desktop file
  fastshot = pkgs.runCommand "fastshot-with-desktop" {} ''
    mkdir -p $out/bin $out/share/applications
    ln -s ${fastshot-bin}/bin/fastshot $out/bin/fastshot
    cp ${fastshot-desktop}/share/applications/*.desktop $out/share/applications/
    chmod 644 $out/share/applications/*.desktop
  '';
in
  fastshot // {
    passthru = {
      bin = fastshot-bin;
      desktop = fastshot-desktop;
    };
  }
