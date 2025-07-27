{pkgs, ...}: let
  fastshot = pkgs.callPackage ./package.nix {};

  fastshotDesktop = pkgs.makeDesktopItem {
    name = "fastshot";
    desktopName = "FastShot";
    noDisplay = true;
    exec = "${fastshot}/bin/fastshot";
    extraConfig = {
      "X-KDE-DBUS-Restricted-Interfaces" = "org.kde.KWin.ScreenShot2";
    };
  };
in {
  environment.systemPackages = [
    (pkgs.symlinkJoin {
      name = "fastshot-with-desktop";
      paths = [fastshot fastshotDesktop];
    })
  ];
}

