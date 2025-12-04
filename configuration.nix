{pkgs, ...}: let
  fastshot = pkgs.callPackage ./default.nix {};

  fastshotDesktop = pkgs.makeDesktopItem {
    name = "fastshot";
    desktopName = "FastShot";
    noDisplay = true;
    exec = "${fastshot}/bin/fastshot";
    extraConfig = {
      "X-KDE-DBUS-Restricted-Interfaces" = "org.kde.KWin.ScreenShot2";
    };
  };

  # Create a proper package with correct permissions
  fastshotWithDesktop = pkgs.runCommand "fastshot-with-desktop" {} ''
    mkdir -p $out/bin $out/share/applications

    # Copy binary
    cp -r ${fastshot}/bin/* $out/bin/

    # Copy desktop file with explicit permissions
    cp ${fastshotDesktop}/share/applications/*.desktop $out/share/applications/
    chmod 644 $out/share/applications/*.desktop
  '';
in {
  environment.systemPackages = [
    fastshotWithDesktop
  ];
}

