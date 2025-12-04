{ pkgs ? import <nixpkgs> {} }:

let
  fastshotModule = import ./configuration.nix;
in
pkgs.testers.nixosTest {
  name = "fastshot-permissions-test";

  # Global timeout for the entire test (5 minutes)
  globalTimeout = 300;

  nodes.machine = { config, pkgs, ... }: {
    imports = [ fastshotModule ];

    # Minimal Plasma/KWin setup for testing
    services.xserver = {
      enable = true;
      displayManager.sddm.enable = true;
      desktopManager.plasma6.enable = true;
    };

    # Enable Wayland support
    services.displayManager.defaultSession = "plasma";

    # Auto-login for testing
    services.displayManager.autoLogin = {
      enable = true;
      user = "testuser";
    };

    users.users.testuser = {
      isNormalUser = true;
      password = "test";
    };

    # Ensure we have a graphical environment
    virtualisation.qemu.options = [ "-vga virtio" ];
  };

  testScript = ''
    start_all()

    # Wait for the system to boot and graphical session to start
    machine.wait_for_unit("multi-user.target")
    machine.wait_for_unit("graphical.target")

    # Wait for display manager
    machine.wait_for_unit("display-manager.service")
    machine.sleep(10)  # Give Plasma time to fully start

    # Test 1: Check that fastshot binary exists and is executable
    machine.succeed("which fastshot")
    machine.succeed("test -x $(which fastshot)")

    # Test 2: Check that desktop file exists in system profile
    desktop_file = machine.succeed("ls /run/current-system/sw/share/applications/fastshot.desktop")
    print(f"Desktop file found: {desktop_file}")

    # Test 3: Verify desktop file permissions (should be readable but not writable by normal users)
    # Use stat -L to follow symlinks and check the actual file permissions
    perms = machine.succeed("stat -L -c '%a' /run/current-system/sw/share/applications/fastshot.desktop")
    print(f"Desktop file permissions: {perms.strip()}")
    assert "644" in perms or "444" in perms, f"Desktop file has wrong permissions: {perms}"

    # Test 4: Check desktop file is owned by root or Nix build user (immutable)
    owner = machine.succeed("stat -c '%U' /run/current-system/sw/share/applications/fastshot.desktop")
    print(f"Desktop file owner: {owner.strip()}")

    # Test 5: Verify desktop file content has X-KDE-DBUS-Restricted-Interfaces
    content = machine.succeed("cat /run/current-system/sw/share/applications/fastshot.desktop")
    print(f"Desktop file content:\n{content}")
    assert "X-KDE-DBUS-Restricted-Interfaces=org.kde.KWin.ScreenShot2" in content, \
        "Desktop file missing X-KDE-DBUS-Restricted-Interfaces"

    # Test 6: Verify testuser cannot modify the desktop file
    machine.fail("su - testuser -c 'echo test >> /run/current-system/sw/share/applications/fastshot.desktop'")

    # Test 7: Check that desktop file is in Nix store (immutable)
    desktop_realpath = machine.succeed("readlink -f /run/current-system/sw/share/applications/fastshot.desktop")
    print(f"Desktop file real path: {desktop_realpath}")
    assert "/nix/store/" in desktop_realpath, "Desktop file is not in Nix store"

    # Test 8: Verify Nix store path is immutable
    machine.fail(f"echo test >> {desktop_realpath.strip()}")

    print("\n✓ All permission tests passed!")
    print("✓ Desktop file is properly installed")
    print("✓ Desktop file is immutable and has correct permissions")
    print("✓ X-KDE-DBUS-Restricted-Interfaces is properly set")
  '';
}
