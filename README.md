# FastShot

Quick PNG CLI screenshot utility for Plasma 6 with SIMD optimizations and secure KWin DBus integration.

## Features

- Fast screenshot capture using KWin's ScreenShot2 DBus interface
- SIMD-optimized (AVX2) BGRA to RGBA conversion
- Low-latency PNG encoding with minimal compression
- Wayland-native (no X11 dependencies)
- Secure desktop file integration with immutable permissions

## Installation

### With Flakes (Recommended)

#### As a NixOS Module

Add to your `configuration.nix`:

```nix
{
  inputs.fastshot.url = "github:yourusername/fastshot";  # Update with your repo

  outputs = { self, nixpkgs, fastshot }: {
    nixosConfigurations.yourhostname = nixpkgs.lib.nixosSystem {
      modules = [
        fastshot.nixosModules.default
        {
          services.fastshot.enable = true;
        }
      ];
    };
  };
}
```

#### As a Package

```bash
# Try it without installing
nix run github:yourusername/fastshot

# Install to your profile
nix profile install github:yourusername/fastshot

# Or add to home-manager
home.packages = [ inputs.fastshot.packages.${system}.default ];
```

### Without Flakes

Add to your `configuration.nix`:

```nix
{
  imports = [ /path/to/fastshot/configuration.nix ];
}
```

Or use the package directly:

```nix
{
  environment.systemPackages = [
    (pkgs.callPackage /path/to/fastshot/default.nix {})
  ];
}
```

## Usage

```bash
# Screenshot to timestamped file (YYYY.MM.DD-HH.MM.SS.png)
fastshot

# Screenshot to specific file
fastshot myscreen.png

# Screenshot to specific path
fastshot ~/Pictures/screenshot.png
```

## Security

FastShot uses KWin's restricted DBus interface `org.kde.KWin.ScreenShot2`. Access to this interface is controlled by:

1. **Desktop file permissions**: The `.desktop` file with `X-KDE-DBUS-Restricted-Interfaces` must be immutable
2. **Nix store immutability**: Desktop file is stored in `/nix/store/` (read-only)
3. **System profile linking**: Only root (via `nixos-rebuild`) can modify the installation

This prevents unauthorized applications from gaining screenshot permissions by modifying the desktop file.

## Development

```bash
# Enter development shell
nix develop

# Build the package
nix build

# Run tests
nix flake check

# Run VM test manually
nix-build test.nix
```

## Testing

See [README-testing.md](./README-testing.md) for details on the NixOS VM test suite that validates:
- Desktop file installation
- Permission security (immutability)
- DBus restriction configuration
- User write protection

## Architecture

- **Language**: C with AVX2 intrinsics
- **Dependencies**: systemd (sd-bus), libpng
- **Build system**: Nix
- **Target**: Linux with KDE Plasma 6 + Wayland

## Performance

- SIMD-optimized color conversion (BGRA→RGBA)
- Fast PNG compression (level 1, no filtering)
- Direct memory-mapped buffer handling
- Zero-copy where possible

## License

MIT

## Contributing

Contributions welcome! Please ensure:
1. Code passes the VM test (`nix flake check`)
2. Security properties are maintained (desktop file immutability)
3. Performance optimizations don't break correctness
