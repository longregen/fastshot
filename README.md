# fastshot

A screenshot utility for KDE Plasma 6 on Wayland. Captures screenshots via KWin's D-Bus interface and saves them as PNG files.

## Features

- Single-shot mode: take one screenshot and exit
- Loop mode: continuous screenshots at configurable intervals
- Image comparison: skips saving duplicate/similar screenshots using SIMD-optimized comparison
- Async PNG writing: screenshots are saved in background threads to avoid blocking capture

## Requirements

- KDE Plasma 6 with KWin Wayland compositor
- systemd (for D-Bus session bus access)
- libpng

## Usage

### Single shot

```
fastshot [output.png]
```

Takes a screenshot of the active screen and saves it. If no filename is provided, uses timestamp format `YYYY.MM.DD-HH.MM.SS.png`.

### Loop mode

```
fastshot --loop [options]
```

Runs continuously, taking screenshots at regular intervals. Only saves when the screen content has changed beyond the similarity threshold.

**Options:**

| Option | Description | Default |
|--------|-------------|---------|
| `--loop` | Enable continuous screenshot mode | - |
| `-d, --directory DIR` | Output directory | `~/desktop-record` |
| `-i, --interval SECS` | Seconds between captures | 45 |
| `-t, --threshold FLOAT` | Similarity threshold (0-1). Only saves if similarity is below this value | 0.99 |
| `-v, --verbose` | Enable verbose logging | off |
| `-h, --help` | Show help | - |

**Example:**

```
fastshot --loop --directory ~/screenshots --interval 10 --threshold 0.95
```

This captures every 10 seconds and saves only when the screen differs by more than 5% from the last saved image.

## NixOS Module

A NixOS module is provided for running fastshot as a user service.

### Configuration

```nix
{
  imports = [ ./path/to/fastshot/module.nix ];

  services.fastshot = {
    enable = true;
    user = "alice";            # required: user to run the service for
    directory = "screenshots";  # relative to $HOME
    interval = 30;
    threshold = 0.99;
    verbose = false;
  };
}
```

The service runs as a systemd user unit, starting after `graphical-session.target`. The `user` option restricts the service to only run for the specified user.

## Building

### With Nix

```
nix build
```

Or enter a development shell:

```
nix develop
```

### Manual

```
cd src
gcc -O3 -mavx2 fastshot.c image-compare.c \
  $(pkg-config --cflags --libs libsystemd libpng) \
  -o fastshot
```

Requires: gcc, pkg-config, libsystemd-dev, libpng-dev

## How it works

1. Connects to the session D-Bus
2. Calls `org.kde.KWin.ScreenShot2.CaptureActiveScreen` to capture the screen
3. Reads BGRA pixel data from the returned pipe
4. In loop mode, compares against the last saved screenshot using MSE-based similarity
5. Converts BGRA to RGBA and writes PNG (async in loop mode)

The image comparison uses AVX2 SIMD instructions when available for fast pixel differencing.

## Files

- `src/fastshot.c` - main program
- `src/image-compare.c` - SIMD-optimized image comparison
- `module.nix` - NixOS service module
- `default.nix` - Nix package definition
- `tests/vm.nix` - NixOS VM integration test

## License

MIT
