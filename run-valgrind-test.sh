#!/usr/bin/env bash
set -e

echo "=== FastShot Valgrind Test ==="

# Use pre-built fastshot
FASTSHOT_BIN="/home/usr/projects/fastshot/result/bin/fastshot"
if [ ! -f "$FASTSHOT_BIN" ]; then
  echo "Error: fastshot binary not found at $FASTSHOT_BIN"
  echo "Please run: nix --extra-experimental-features 'nix-command flakes' build .#fastshot"
  exit 1
fi

# Create temp directory
TEMP_DIR=$(mktemp -d)
echo "Using temp directory: $TEMP_DIR"

# Start DBus session
export DBUS_SESSION_BUS_ADDRESS="unix:path=$TEMP_DIR/dbus-session"
echo "Starting DBus daemon..."
dbus-daemon --session --nofork --nopidfile --address="$DBUS_SESSION_BUS_ADDRESS" --print-address &
DBUS_PID=$!
sleep 2

# Start mock KWin service
echo "Starting mock KWin service..."
python3 ./mock-kwin-dbus.py &
MOCK_PID=$!
sleep 3

# Run fastshot under valgrind
echo ""
echo "Running fastshot under valgrind..."
cd "$TEMP_DIR"

valgrind \
  --leak-check=full \
  --show-leak-kinds=all \
  --track-origins=yes \
  --verbose \
  --error-exitcode=42 \
  --log-file=valgrind.log \
  $FASTSHOT_BIN test-screenshot.png

VALGRIND_EXIT=$?

# Check if screenshot was created
echo ""
if [ -f test-screenshot.png ]; then
  echo "✓ Screenshot created successfully"
  ls -lh test-screenshot.png

  # Verify it's a valid PNG
  if file test-screenshot.png | grep -q PNG; then
    echo "✓ Valid PNG file"
  else
    echo "✗ Invalid PNG file"
    cat valgrind.log
    kill $MOCK_PID $DBUS_PID 2>/dev/null || true
    exit 1
  fi
else
  echo "✗ Screenshot file was not created"
  cat valgrind.log
  kill $MOCK_PID $DBUS_PID 2>/dev/null || true
  exit 1
fi

# Show valgrind output
echo ""
echo "=== Valgrind Output ==="
cat valgrind.log

# Clean up
kill $MOCK_PID $DBUS_PID 2>/dev/null || true
cd /home/usr/projects/fastshot
rm -rf "$TEMP_DIR"

# Check valgrind results
if [ $VALGRIND_EXIT -eq 0 ]; then
  echo ""
  echo "=== Valgrind Test PASSED ==="
  echo "✓ No memory leaks detected"
  echo "✓ No invalid memory access"
  echo "✓ All memory properly freed"
  exit 0
else
  echo ""
  echo "=== Valgrind Test FAILED ==="
  echo "✗ Memory errors detected (exit code: $VALGRIND_EXIT)"
  exit 1
fi
