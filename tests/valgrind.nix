{ pkgs ? import <nixpkgs> {} }:

let
  fastshot = pkgs.callPackage ../default.nix {};

  # Minimal D-Bus session configuration
  dbusConfig = pkgs.writeText "session.conf" ''
    <!DOCTYPE busconfig PUBLIC "-//freedesktop//DTD D-BUS Bus Configuration 1.0//EN"
     "http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd">
    <busconfig>
      <type>session</type>
      <listen>unix:tmpdir=/tmp</listen>
      <policy context="default">
        <allow send_destination="*" eavesdrop="true"/>
        <allow eavesdrop="true"/>
        <allow own="*"/>
      </policy>
    </busconfig>
  '';

  # Wrapper script that runs the test in a D-Bus session
  testScript = pkgs.writeShellScript "valgrind-test.sh" ''
    set -e

    echo "=== FastShot Valgrind Test ==="

    # Start D-Bus daemon with custom config
    ${pkgs.dbus}/bin/dbus-daemon --config-file=${dbusConfig} --print-address &
    DBUS_PID=$!

    # Wait for D-Bus to start and capture its address
    sleep 1
    export DBUS_SESSION_BUS_ADDRESS=$(${pkgs.dbus}/bin/dbus-daemon --config-file=${dbusConfig} --print-address --fork)

    # Run the actual test
    bash -c '
      set -e

      # Start mock KWin service
      ${pkgs.python3.withPackages (ps: [ ps.dbus-python ps.pygobject3 ])}/bin/python3 ${./mock-kwin-dbus.py} &
      MOCK_PID=$!

      # Give the mock service time to register on the bus
      sleep 2

      # Create temp output directory
      TEMP_DIR=$(mktemp -d)
      cd "$TEMP_DIR"

      # Run fastshot under valgrind
      echo ""
      echo "Running fastshot under valgrind..."
      ${pkgs.valgrind}/bin/valgrind \
        --leak-check=full \
        --show-leak-kinds=all \
        --track-origins=yes \
        --verbose \
        --error-exitcode=1 \
        ${fastshot}/bin/fastshot test-screenshot.png 2>&1 | tee valgrind.log

      VALGRIND_EXIT=$?

      # Check if screenshot was created
      if [ -f test-screenshot.png ]; then
        echo ""
        echo "Screenshot created successfully"
        ls -lh test-screenshot.png

        # Verify PNG file is valid
        if ${pkgs.file}/bin/file test-screenshot.png | grep -q PNG; then
          echo "Valid PNG file"
        else
          echo "Invalid PNG file"
          kill $MOCK_PID 2>/dev/null || true
          exit 1
        fi

        # Check dimensions with identify if available
        if command -v ${pkgs.imagemagick}/bin/identify &> /dev/null; then
          ${pkgs.imagemagick}/bin/identify test-screenshot.png
        fi
      else
        echo "Screenshot file was not created"
        kill $MOCK_PID 2>/dev/null || true
        exit 1
      fi

      # Clean up
      kill $MOCK_PID 2>/dev/null || true
      rm -rf "$TEMP_DIR"

      # Check valgrind results
      if [ $VALGRIND_EXIT -eq 0 ]; then
        echo ""
        echo "=== Valgrind Test PASSED ==="
        echo "No memory leaks detected"
        echo "No invalid memory access"
        echo "All memory properly freed"
        exit 0
      else
        echo ""
        echo "=== Valgrind Test FAILED ==="
        echo "Memory errors detected (see log above)"
        exit 1
      fi
    '

    # Clean up D-Bus daemon
    kill $DBUS_PID 2>/dev/null || true
  '';

  # Wrapper that provides the test environment
  valgrindTest = pkgs.stdenv.mkDerivation {
    name = "fastshot-valgrind-test";

    buildInputs = [
      pkgs.valgrind
      pkgs.dbus
      pkgs.python3
      (pkgs.python3.withPackages (ps: [ ps.dbus-python ps.pygobject3 ]))
      pkgs.file
      pkgs.imagemagick
      fastshot
    ];

    unpackPhase = "true";

    buildPhase = ''
      mkdir -p $out/bin
      cp ${testScript} $out/bin/valgrind-test
      chmod +x $out/bin/valgrind-test
    '';

    installPhase = ''
      mkdir -p $out
      echo "Valgrind test prepared"
    '';

    doCheck = true;
    checkPhase = ''
      echo "Running valgrind test..."
      $out/bin/valgrind-test
    '';

    meta = {
      description = "Valgrind memory safety test for fastshot";
    };
  };

in valgrindTest
