#!/usr/bin/env python3
"""
Mock KWin ScreenShot2 DBus service for testing fastshot with valgrind.
This allows us to test memory safety without requiring a full KDE Plasma session.
"""

import dbus
import dbus.service
import dbus.mainloop.glib
from gi.repository import GLib
import os
import tempfile
import struct

class MockScreenShot2(dbus.service.Object):
    """Mock implementation of org.kde.KWin.ScreenShot2 interface"""

    def __init__(self, bus_name):
        super().__init__(bus_name, '/org/kde/KWin/ScreenShot2')

    @dbus.service.method(
        dbus_interface='org.kde.KWin.ScreenShot2',
        in_signature='a{sv}h',
        out_signature='a{sv}'
    )
    def CaptureActiveScreen(self, options, fd):
        """
        Mock CaptureActiveScreen that writes a small test pattern to the provided fd.
        Returns metadata matching KWin's real interface.
        """
        # Extract the actual file descriptor from dbus.UnixFd
        # The take() method returns the integer fd and takes ownership
        actual_fd = fd.take()

        # Generate a small 100x100 BGRA test pattern
        width = 100
        height = 100
        stride = width * 4  # 4 bytes per pixel (BGRA)

        # Create a simple gradient pattern in BGRA format
        # This helps detect color conversion bugs
        pixels = bytearray()
        for y in range(height):
            for x in range(width):
                b = (x * 255) // width  # Blue gradient horizontal
                g = (y * 255) // height  # Green gradient vertical
                r = 128  # Constant red
                a = 255  # Fully opaque
                pixels.extend([b, g, r, a])

        # Write to the file descriptor
        try:
            os.write(actual_fd, pixels)
        finally:
            os.close(actual_fd)

        # Return metadata dictionary matching KWin's format
        return {
            'width': dbus.UInt32(width),
            'height': dbus.UInt32(height),
            'stride': dbus.UInt32(stride),
            'format': dbus.UInt32(0)  # 0 = BGRA format
        }

def main():
    """Start the mock DBus service"""
    dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)

    # Use session bus (user bus)
    bus = dbus.SessionBus()
    name = dbus.service.BusName('org.kde.KWin.ScreenShot2', bus)

    service = MockScreenShot2(name)

    print("Mock KWin.ScreenShot2 service started on session bus")
    print("Ready to receive CaptureActiveScreen calls")

    loop = GLib.MainLoop()
    try:
        loop.run()
    except KeyboardInterrupt:
        print("\nShutting down mock service")

if __name__ == '__main__':
    main()
