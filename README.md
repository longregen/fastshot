# Fastshot

A high-performance screenshot utility for KDE Plasma that captures screenshots using D-Bus and KWin's screenshot interface. It supports both single-shot captures and continuous monitoring with duplication detection.

## Features

- `fastshot` will create a screenshot using the D-Bus interface of the current active screen. The image is written into the memory of the program, and encoded to a PNG file with the current date + time.
- `fastshot --loop` will continuously capture screenshots at regular intervals (`-i 40s` means "every 40 seconds").
- `fastshot --loop` will not save two very similar images. `-t 0.99` is the default "threshold" for similarity (uses mean squared error between pixels of the last saved image and the current screenshot). "1" means equal images; "0" means completely different.
- Can run as a systemd user service.

## Usage

### Single Shot Mode

Capture a single screenshot:
```bash
fastshot
```

If no filename is provided, it generates one with the current timestamp (e.g., `2025.07.30-14.32.15.png`). To select a name:

```bash
fastshot output_file.png
```

### Loop Mode

Continuously capture screenshots:
```bash
fastshot --loop [options]

```

#### Loop Mode Options
- `-d, --directory DIR` - Target directory for screenshots (default: `~/desktop-record`)
- `-i, --interval SECS` - Screenshot interval in seconds (default: 45)
- `-t, --threshold FLOAT` - Similarity threshold 0-1 (default: 0.99)
- `-v, --verbose` - Enable verbose logging
- `-h, --help` - Show help message

### Examples

```bash
# Loop mode with default settings
fastshot --loop

# Loop mode with custom settings
fastshot --loop -d /path/to/screenshots -i 30 -t 0.95 -v
```

## How It Works

### Screenshot Capture

Fastshot uses KDE's D-Bus interface (`org.kde.KWin.ScreenShot2`) to capture screenshots.

### Duplicate Detection

In loop mode, Fastshot compares each new screenshot with the last saved one using:
- SIMD-optimized Mean Squared Error (MSE) calculation
- Configurable similarity threshold (0-1, where 1 = identical)
- Only saves screenshots that differ significantly from the previous one

### File Format

Screenshots are saved as PNG files with:
- BGRA color format
- Fast compression settings (level 1)
- Timestamp-based filenames: `YYYY.MM.DD-HH.MM.SS.png`

## Building

### Dependencies
- systemd (for sd-bus)
- libpng
- pthread
- libavutil (for image utilities)
- C compiler with SSE/AVX support

### NixOS/Nix
This project includes a Nix flake.

```bash
# Build the package
nix build

# Enter development shell
nix develop

# Run directly from flake
nix run github:longregen/fastshot
```

## NixOS Module

The project includes a NixOS module for running Fastshot as a systemd user service:

```nix
{
  imports = [ path/to/fastshot/nixos-module.nix ];
  
  behaviors.screenshot-loop = {
    enable = true;
    frequency = "30s";  # Supports s/m/h suffixes
    threshold = 0.95;
  };
}
```

The service:
- Starts with the graphical session
- Waits for the compositor to be ready
- Automatically creates the screenshot directory
- Restarts on failure
- Properly imports display environment variables

## Architecture

### Core Components

1. **fastshot.c** - Main application logic
   - Argument parsing and configuration
   - D-Bus connection management
   - Screenshot capture coordination
   - Loop mode implementation

2. **image-compare.c** - Image comparison algorithms
   - SIMD-optimized MSE calculation
   - BGRA pixel comparison
   - Similarity scoring

3. **test-image-compare.c** - Unit tests for image comparison

### Performance Optimizations

- **Memory-mapped I/O**: Uses `memfd_create` for zero-copy screenshot transfer
- **Async PNG Writing**: Detached threads handle file I/O without blocking capture
- **SIMD Instructions**: Uses SSE/AVX for fast pixel comparison
- **Fast PNG Settings**: Minimal compression for quick saves

## License

[MIT](https://mit-license.org/)
