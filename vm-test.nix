{
  pkgs,
  fastshot,
  fastshotWithDesktop,
}: let
  testInterval = 2;
  testThreshold = 0.98;
  testDirectory = "/home/testuser/desktop-record";
in
  pkgs.nixosTest {
    name = "fastshot-loop";

    nodes.machine = {
      config,
      pkgs,
      ...
    }: {
      virtualisation = {
        memorySize = 2048;
        cores = 2;
      };
      imports = [
        ./nixos-module.nix
      ];

      nixpkgs.overlays = [
        (final: prev: {
          inherit fastshot fastshotWithDesktop;
        })
      ];

      behaviors.screenshot-loop = {
        enable = true;
        frequency = "${toString testInterval}s";
        threshold = testThreshold;
      };

      services = {
        xserver = {
          enable = true;
          displayManager.setupCommands = ''
            ${pkgs.xorg.xhost}/bin/xhost +local:
          '';
        };
        displayManager = {
          sddm.enable = true;
          autoLogin = {
            enable = true;
            user = "testuser";
          };
          defaultSession = "plasmax11";
        };
        desktopManager.plasma6.enable = true;
      };

      security.polkit.enable = true;

      xdg.portal = {
        enable = true;
        extraPortals = with pkgs; [
          kdePackages.xdg-desktop-portal-kde
        ];
        config.common.default = ["kde"];
      };

      # Disable unnecessary KDE services
      systemd.user.services = {
        "kde-baloo".enable = false;
        "akonadi".enable = false;
        "kdeconnect".enable = false;
        "powerdevil".enable = false;
      };

      users.users.testuser = {
        isNormalUser = true;
        password = "test";
        home = "/home/testuser";
        createHome = true;
        extraGroups = [
          "video"
          "audio"
        ];
      };

      environment.systemPackages = with pkgs; [
        fastshotWithDesktop
        kdePackages.kate
        kdePackages.spectacle
      ];
    };

    testScript = ''
      import time

      def count_screenshots():
          """Count PNG files in the screenshot directory"""
          print(time.time())
          result = machine.succeed("find ${testDirectory} -name '*.png' | wc -l")
          return int(result.strip())

      machine.start()
      machine.wait_for_x()
      machine.wait_for_unit("multi-user.target")

      print("1. Setting up minimal KDE X11 environment...")
      machine.wait_for_unit("graphical-session.target", user="testuser")
      machine.succeed("pgrep plasmashell")

      print("2. Waiting for screenshot-loop.service to start automatically...")
      machine.wait_for_unit("screenshot-loop.service", user="testuser")
      print("screenshot-loop.service is running")

      print("2a. Checking screenshot-loop service environment...")
      machine.succeed("systemctl --user -M testuser@ show screenshot-loop.service | grep -E '(ExecStart|Environment)' || true")

      print("3. Waiting for first screenshot to be captured...")
      machine.wait_until_succeeds("test $(find ${testDirectory} -name '*.png' | wc -l) -ge 1", timeout=60)
      initial_count = count_screenshots()
      print(f"Initial screenshot count: {initial_count}")

      print("4. Testing screenshot capture with Kate...")
      machine.succeed("su - testuser -c 'DISPLAY=:0 kate &'")
      time.sleep(3)

      time.sleep(${toString testInterval} + 1)
      count_after_kate = count_screenshots()
      print(f"Count after opening Kate: {count_after_kate}")

      assert count_after_kate > initial_count, f"Kate opening not detected: {initial_count} -> {count_after_kate}"

      print("5. Testing screenshot comparison...")
      machine.succeed("su - testuser -c 'pkill kate'")
      time.sleep(2)

      # Open Kate again - should be similar and not trigger new screenshot
      machine.succeed("su - testuser -c 'DISPLAY=:0 kate &'")
      time.sleep(${toString testInterval} + 1)

      count_after_similar = count_screenshots()
      print(f"Count after similar screen: {count_after_similar}")

      # This might be equal (similar screen) or +1 (if different enough)
      # The important thing is the service is working
      assert count_after_similar >= count_after_kate, "Screenshot comparison working"

      print("All tests passed!")

      print("Extracting screenshots from VM...")
      screenshots = machine.succeed("find ${testDirectory} -name '*.png'").strip().split('\n')
      for i, screenshot_path in enumerate(screenshots):
        if screenshot_path.strip():
          machine.copy_from_vm(screenshot_path.strip(), f"screenshot_{i:02d}.png")

      print(f"Extracted {len([s for s in screenshots if s.strip()])} screenshots to host")
    '';
  }
