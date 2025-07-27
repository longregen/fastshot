# Microshot - Ultra-fast Screenshot Tool

Minimal, performance-focused screenshot utility for Linux with SIMD optimizations.

## Features

- **Ultra-fast**: SIMD-optimized image processing (AVX2/SSE3)
- **Multi-backend**: X11, Wayland (KDE/GNOME DBus), wlr-screencopy  
- **Auto-naming**: Timestamps filenames when none provided
- **Minimal dependencies**: ~50KB binary
- **Static builds**: Support for musl/Alpine containers

## Performance

- KDE Wayland: ~50-100ms for 4K screenshot
- X11: ~100-150ms with SIMD conversion
- Memory efficient: Single-pass processing

## Build

```bash
# Standard build
nix-build

# With wlr-screencopy support (sway, hyprland, etc)
nix-build --arg enableWlroots true

# Static binary for containers
nix-build --arg enableStatic true --arg enableMusl true

# Manual build
gcc -O3 -march=native -msse3 -mavx2 microshot.c \
    $(pkg-config --cflags --libs x11 libpng dbus-1) \
    -o microshot
```

## Usage

```bash
# Auto-named screenshot (YYYY-MM-DD_HH-MM-SS.png)
microshot

# Named screenshot
microshot screenshot.png
```

## Optimizations

1. **SIMD BGRA→RGBA**: AVX2 processes 8 pixels/cycle, SSE3 processes 4 pixels/cycle
2. **PNG Compression**: Level 1 for speed/size balance
3. **Compiler flags**: `-O3 -march=native -flto -ffast-math`
4. **Zero-copy KDE path**: Direct file operations via sendfile()
5. **Reduced timeouts**: 500ms DBus timeouts for faster fallbacks