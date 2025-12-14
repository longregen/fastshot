{pkgs ? import <nixpkgs> {}}:
pkgs.testers.nixosTest {
  name = "fastshot-test";

  globalTimeout = 600;

  nodes.machine = {pkgs, ...}: {
    imports = [../module.nix];

    services.fastshot = {
      enable = true;
      directory = "screenshots";
      interval = 5;
      threshold = 0.99;
      verbose = true;
    };

    services = {
      xserver = {
        enable = true;
        desktopManager.plasma6.enable = true;
      };
      displayManager = {
        sddm.enable = true;
        defaultSession = "plasma";
        autoLogin = {
          enable = true;
          user = "testuser";
        };
      };
    };

    users.users.testuser = {
      isNormalUser = true;
      password = "test";
    };

    virtualisation.qemu.options = ["-vga virtio"];
    virtualisation.memorySize = 4096;
  };

  testScript = ''
    start_all()

    machine.wait_for_unit("multi-user.target")
    machine.wait_for_unit("graphical.target")
    machine.wait_for_unit("display-manager.service")
    machine.sleep(10)

    # Wait for fastshot service to be running
    machine.wait_until_succeeds("systemctl --user -M testuser@ is-active fastshot.service", timeout=60)

    machine.sleep(20)

    # Copy screenshots to xchg
    machine.succeed("cp -r /home/testuser/screenshots /tmp/xchg/")

    # Verify at least one screenshot exists
    machine.succeed("ls /tmp/xchg/screenshots/*.png")
  '';
}
